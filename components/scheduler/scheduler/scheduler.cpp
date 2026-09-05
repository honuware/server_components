#include "scheduler.h"

#include <csignal>
#include <exception>
#include <utility>

#include <boost/system/error_code.hpp>

#include "util/logging.h"

namespace Scheduler {

Scheduler::Scheduler(std::vector<ScheduledJob> jobs, Http::HttpClientPtr httpClient)
    : jobs_(std::move(jobs)),
      jobScheduler_(std::make_unique<JobScheduler>(std::move(httpClient))) {}

Scheduler::Scheduler(
    JobListProvider provider,
    std::chrono::seconds refreshInterval,
    Http::HttpClientPtr httpClient)
    : jobScheduler_(std::make_unique<JobScheduler>(std::move(httpClient))),
      provider_(std::move(provider)),
      refreshInterval_(refreshInterval) {}

Scheduler::~Scheduler() {
    Shutdown();
}

bool Scheduler::Initialize() {
    if (provider_) {
        // Build the initial job set from the provider. A throwing provider (e.g.
        // the control database is unreachable at startup) propagates to main,
        // which exits non-zero so the container is retried.
        jobs_ = provider_();
        lastJobs_ = jobs_;
    }
    for (auto& job : jobs_) {
        jobScheduler_->RegisterJob(std::move(job));
    }

    // AuthenticateTargets logs in once per distinct (serverUrl, email,
    // password) target and emits [scheduler] event=authenticate_failed with
    // the server/email on any failure, so we don't double-log the attempt here.
    // We do log the outcome at this layer because operators reading the
    // [scheduler] stream want to see whether Initialize succeeded without
    // correlating across two prefixes.
    if (!jobScheduler_->AuthenticateTargets()) {
        LogError() << "[scheduler] event=initialize_failed reason=login_failed\n";
        return false;
    }

    LogInfo() << "[scheduler] event=initialized jobs="
              << jobScheduler_->GetJobs().size() << "\n";
    initialized_ = true;
    return true;
}

void Scheduler::Run() {
    if (!initialized_) {
        LogError() << "[scheduler] event=run_before_initialize\n";
        return;
    }
    InstallSignalHandlers();
    jobScheduler_->Start(ioContext_);
    ScheduleRefresh();
    LogInfo() << "[scheduler] event=event_loop_starting\n";
    ioContext_.run();
    LogInfo() << "[scheduler] event=event_loop_exited\n";
    jobScheduler_->Stop();
}

bool Scheduler::Refresh() {
    if (!provider_) return false;

    std::vector<ScheduledJob> next;
    try {
        next = provider_();
    } catch (const std::exception& e) {
        // Keep running the current set — a transient control-DB error must not
        // take the scheduler down. The next tick tries again.
        LogError() << "[scheduler] event=refresh_failed error=\"" << e.what()
                   << "\" action=keep_current_jobs\n";
        return false;
    }

    if (next == lastJobs_) {
        return false;  // no tenant added/removed/changed — nothing to do
    }

    LogInfo() << "[scheduler] event=refresh_reloading"
              << " old_jobs=" << lastJobs_.size()
              << " new_jobs=" << next.size() << "\n";

    lastJobs_ = next;
    // Stop → swap → re-authenticate → restart, mirroring RunFor's re-arm pattern.
    // The JobScheduler object stays alive, so no in-flight timer callback can
    // dangle; SetJobs() drops sessions for tenants that vanished.
    jobScheduler_->Stop();
    jobScheduler_->SetJobs(std::move(next));
    if (!jobScheduler_->AuthenticateTargets()) {
        // A new tenant failed to authenticate. Log and keep serving the tenants
        // that did — Start() below arms timers for every job whose target
        // authenticated; the failed target's calls just 401 until the next
        // refresh. (A hard exit would punish healthy tenants for one bad row.)
        LogError() << "[scheduler] event=refresh_authenticate_partial\n";
    }
    jobScheduler_->Start(ioContext_);
    return true;
}

void Scheduler::ScheduleRefresh() {
    if (!provider_ || refreshInterval_.count() <= 0) return;
    if (!refreshTimer_) {
        refreshTimer_ =
            std::make_unique<boost::asio::steady_timer>(ioContext_);
    }
    refreshTimer_->expires_after(refreshInterval_);
    refreshTimer_->async_wait([this](const boost::system::error_code& ec) {
        if (ec) return;  // cancelled by Shutdown — nothing to do
        (void)Refresh();
        ScheduleRefresh();  // re-arm for the next interval
    });
}

void Scheduler::RunFor(std::chrono::milliseconds duration) {
    if (!initialized_) {
        LogError() << "[scheduler] event=runfor_before_initialize\n";
        return;
    }
    // run_for can leave the io_context in a "stopped" state if it ran out
    // of work; restart() rearms it for repeated test invocations.
    if (ioContext_.stopped()) {
        ioContext_.restart();
    }
    jobScheduler_->Start(ioContext_);
    ioContext_.run_for(duration);
    jobScheduler_->Stop();
}

void Scheduler::Shutdown() {
    if (jobScheduler_) {
        jobScheduler_->Stop();
    }
    if (refreshTimer_) {
        // Boost 1.91 removed basic_waitable_timer::cancel(error_code&) -- see the
        // same change in JobScheduler::Stop. Note the asymmetry with signals_
        // below: signal_set KEPT its error_code overload, so that call is
        // deliberately left alone rather than made to match this one.
        refreshTimer_->cancel();
    }
    if (!ioContext_.stopped()) {
        ioContext_.stop();
    }
    if (signals_) {
        boost::system::error_code ec;
        signals_->cancel(ec);
    }
}

void Scheduler::InstallSignalHandlers() {
    signals_ = std::make_unique<boost::asio::signal_set>(
        ioContext_, SIGINT, SIGTERM);
    signals_->async_wait(
        [this](const boost::system::error_code& ec, int signal) {
            if (ec) return;  // cancelled — nothing to do
            LogInfo() << "[scheduler] event=signal_received signal=" << signal
                      << " action=graceful_shutdown\n";
            Shutdown();
        });
}

}  // namespace Scheduler
