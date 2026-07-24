#include "job_scheduler.h"

#include <chrono>
#include <set>
#include <stdexcept>
#include <utility>

#include <boost/asio/error.hpp>
#include <boost/system/error_code.hpp>

#include "util/http/http_client.h"
#include "util/logging.h"

namespace Scheduler {

JobScheduler::JobScheduler(Http::HttpClientPtr httpClient)
    : httpClient_(std::move(httpClient)) {}

JobScheduler::~JobScheduler() {
    Stop();
}

void JobScheduler::RegisterJob(ScheduledJob job) {
    if (job.intervalSeconds <= 0) return;
    jobs_.push_back(std::move(job));
}

void JobScheduler::SetJobs(std::vector<ScheduledJob> jobs) {
    jobs_.clear();
    for (auto& job : jobs) {
        if (job.intervalSeconds <= 0) continue;
        jobs_.push_back(std::move(job));
    }
    // Drop every cached session: a tenant that vanished from the new set must
    // lose its client, and the surviving tenants are re-authenticated by the
    // AuthenticateTargets() the caller runs next. (Refresh only reloads on an
    // actual change, so this re-login cost is paid rarely.)
    clientsByTarget_.clear();
}

std::string JobScheduler::TargetKey(
    const std::string& serverUrl,
    const std::string& serviceAccountEmail,
    const std::string& serviceAccountPassword,
    const std::vector<std::pair<std::string, std::string>>& headers) {
    // Newline-joined so no combination of URL/email/password can collide with
    // a different combination (URLs and emails can't contain a newline). The
    // headers are appended in order — the catalog builds them deterministically
    // per tenant — so two jobs for the same tenant hash equal while jobs for
    // different tenants (different X-Honuware-Site) hash distinct.
    std::string key =
        serverUrl + "\n" + serviceAccountEmail + "\n" + serviceAccountPassword;
    for (const auto& [name, value] : headers) {
        key += "\n";
        key += name;
        key += ": ";
        key += value;
    }
    return key;
}

ApiClientPtr JobScheduler::EnsureClient(const ScheduledJob& job) {
    std::string key = TargetKey(
        job.serverUrl, job.serviceAccountEmail, job.serviceAccountPassword,
        job.headers);
    auto it = clientsByTarget_.find(key);
    if (it != clientsByTarget_.end()) return it->second;
    // The job's headers become the client's login headers so the /api/login it
    // issues carries this tenant's X-Honuware-Site (control mode rejects a
    // header-less login).
    auto client =
        std::make_shared<ApiClient>(httpClient_, job.serverUrl, job.headers);
    clientsByTarget_[key] = client;
    return client;
}

ApiClientPtr JobScheduler::GetClientForTarget(
    const std::string& serverUrl,
    const std::string& serviceAccountEmail,
    const std::string& serviceAccountPassword,
    const std::vector<std::pair<std::string, std::string>>& headers) const {
    auto it = clientsByTarget_.find(TargetKey(
        serverUrl, serviceAccountEmail, serviceAccountPassword, headers));
    if (it == clientsByTarget_.end()) return nullptr;
    return it->second;
}

bool JobScheduler::AuthenticateTargets() {
    bool allOk = true;
    std::set<std::string> attempted;
    for (const auto& job : jobs_) {
        std::string key = TargetKey(
            job.serverUrl, job.serviceAccountEmail, job.serviceAccountPassword,
            job.headers);
        if (!attempted.insert(key).second) continue;  // already handled this target
        ApiClientPtr client = EnsureClient(job);
        if (!client->Login(job.serviceAccountEmail, job.serviceAccountPassword)) {
            LogError() << "[scheduler] event=authenticate_failed"
                       << " server_url=" << job.serverUrl
                       << " email=" << job.serviceAccountEmail << "\n";
            allOk = false;
        }
    }
    return allOk;
}

const ScheduledJob* JobScheduler::FindJob(const std::string& jobName) const {
    for (const auto& job : jobs_) {
        if (job.name == jobName) return &job;
    }
    return nullptr;
}

JobExecutionResult JobScheduler::RunJobOnce(const std::string& jobName) {
    const ScheduledJob* job = FindJob(jobName);
    if (job == nullptr) {
        throw std::out_of_range("JobScheduler::RunJobOnce - unknown job: " + jobName);
    }

    JobExecutionResult result;
    result.jobName = job->name;

    ApiClientPtr client = EnsureClient(*job);
    auto start = std::chrono::steady_clock::now();
    try {
        Http::HttpResponse response =
            client->CallEndpoint(job->method, job->path, "", job->headers);
        result.statusCode = response.statusCode;
        result.success = (response.statusCode >= 200 && response.statusCode < 300);
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = e.what();
    } catch (...) {
        result.success = false;
        result.errorMessage = "unknown exception";
    }
    auto end = std::chrono::steady_clock::now();
    result.durationMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    if (result.success) {
        LogInfo() << "[scheduler] event=job_success"
                  << " job=" << result.jobName
                  << " status=" << result.statusCode
                  << " duration_ms=" << result.durationMs.count() << "\n";
    } else if (!result.errorMessage.empty()) {
        LogError() << "[scheduler] event=job_exception"
                   << " job=" << result.jobName
                   << " error=\"" << result.errorMessage << "\""
                   << " duration_ms=" << result.durationMs.count() << "\n";
    } else {
        LogError() << "[scheduler] event=job_http_error"
                   << " job=" << result.jobName
                   << " status=" << result.statusCode
                   << " duration_ms=" << result.durationMs.count() << "\n";
    }

    return result;
}

void JobScheduler::Start(boost::asio::io_context& ioContext) {
    if (started_) return;
    started_ = true;
    timers_.clear();
    timers_.reserve(jobs_.size());
    for (size_t i = 0; i < jobs_.size(); ++i) {
        timers_.push_back(std::make_unique<boost::asio::steady_timer>(ioContext));
        ScheduleNext(ioContext, i);
    }
}

void JobScheduler::Stop() {
    started_ = false;
    for (auto& timer : timers_) {
        if (timer) {
            boost::system::error_code ec;
            timer->cancel(ec);
        }
    }
    timers_.clear();
}

void JobScheduler::ScheduleNext(
    boost::asio::io_context& ioContext, size_t jobIndex) {
    if (!started_ || jobIndex >= timers_.size() || !timers_[jobIndex]) return;
    const ScheduledJob& job = jobs_[jobIndex];
    auto* timer = timers_[jobIndex].get();
    timer->expires_after(std::chrono::seconds(job.intervalSeconds));
    timer->async_wait([this, &ioContext, jobIndex](
            const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;  // Stop() cancelled us — nothing to do.
        }
        if (!started_) return;
        if (ec) {
            LogError() << "[scheduler] event=timer_error"
                       << " job_index=" << jobIndex
                       << " message=\"" << ec.message() << "\"\n";
        } else {
            // ApiClient is single-thread-safe within the io_context callback.
            (void)RunJobOnce(jobs_[jobIndex].name);
        }
        ScheduleNext(ioContext, jobIndex);
    });
}

}  // namespace Scheduler
