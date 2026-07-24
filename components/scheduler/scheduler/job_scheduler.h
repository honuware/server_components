#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include "api_client.h"
#include "scheduled_job.h"
#include "util/http/http_client.h"

namespace Scheduler {

// Owns a set of ScheduledJob definitions and drives their execution.
//
// Each ScheduledJob is fully self-describing (target URL + credentials +
// headers), so the JobScheduler stays app-agnostic: it groups registered jobs
// by their (serverUrl, serviceAccountEmail, serviceAccountPassword) target,
// maintains one ApiClient (one cookie jar) per distinct target, and never needs
// to know what a "tenant" is. A single HttpClient transport is shared by every
// target's ApiClient; only the cached session cookies differ.
//
// Two execution paths:
//   - RunJobOnce(name): synchronous, suitable for tests and for ad-hoc
//     "run this job right now" use. Catches exceptions thrown by the
//     ApiClient, surfaces them as result.errorMessage with success=false,
//     and logs the outcome.
//   - Start(io_context): registers a boost::asio::steady_timer per job
//     that fires every interval and invokes RunJobOnce. Stop() cancels
//     all timers — call before letting the io_context unwind.
//
// Wall-clock alignment (e.g. "run at 1 AM daily" vs. "every 24h from
// startup") is intentionally not implemented in this phase. The plan
// section 3.3 covers it but we kept the surface area small here; jobs
// fire on monotonic intervals from Start().
class JobScheduler {
public:
    explicit JobScheduler(Http::HttpClientPtr httpClient);
    ~JobScheduler();

    JobScheduler(const JobScheduler&) = delete;
    JobScheduler& operator=(const JobScheduler&) = delete;

    // Adds a job. Jobs whose interval <= 0 are silently dropped, since the
    // operator's intent of "interval=0 means disabled" is satisfied either
    // here or upstream in the app-side catalog — both are convenient entry
    // points so we accept disabled jobs at both sides.
    void RegisterJob(ScheduledJob job);

    // Replaces the entire job set (control-mode tenant refresh). Disabled jobs
    // (interval <= 0) are dropped, and the per-target ApiClient/session cache is
    // cleared so a removed tenant's session is dropped and the next
    // AuthenticateTargets() logs the current set in fresh. The caller must Stop()
    // the timers before calling this and AuthenticateTargets() + Start() after.
    void SetJobs(std::vector<ScheduledJob> jobs);

    const std::vector<ScheduledJob>& GetJobs() const { return jobs_; }

    // Logs in to every distinct (serverUrl, email, password) target used by a
    // registered job, caching one authenticated ApiClient per target. Returns
    // false if ANY target's login fails — callers (Scheduler::Initialize)
    // should treat that as fatal so the process exits non-zero and gets
    // retried. Idempotent: a target already cached is not re-logged-in.
    bool AuthenticateTargets();

    // Synchronously executes the named job exactly once via the ApiClient for
    // its target (creating one if AuthenticateTargets hasn't run yet), logs the
    // outcome, and returns the result. Throws std::out_of_range if no job by
    // that name has been registered. Network/HTTP errors do NOT propagate —
    // they're captured in the returned result.
    JobExecutionResult RunJobOnce(const std::string& jobName);

    // Sets up a steady_timer per registered job in the supplied io_context.
    // Each timer fires after intervalSeconds, runs the job, and reschedules
    // itself. Caller is responsible for driving the io_context (run() /
    // run_for() / etc.).
    void Start(boost::asio::io_context& ioContext);

    // Cancels all timers. Pending callbacks may still fire once with an
    // operation_aborted error, which the implementation handles silently.
    void Stop();

    // Inspection helper (primarily for tests): the ApiClient serving the given
    // target, or nullptr if no job for that target has been created yet. The
    // headers participate in the target identity (two otherwise-identical targets
    // with different X-Honuware-Site headers are distinct sessions), so pass the
    // job's headers to look up a per-tenant client; the default empty headers
    // match the single-tenant deployment.
    ApiClientPtr GetClientForTarget(
        const std::string& serverUrl,
        const std::string& serviceAccountEmail,
        const std::string& serviceAccountPassword,
        const std::vector<std::pair<std::string, std::string>>& headers = {}) const;

private:
    void ScheduleNext(boost::asio::io_context& ioContext, size_t jobIndex);

    // Look up a registered job by name. Returns nullptr if not found.
    const ScheduledJob* FindJob(const std::string& jobName) const;

    // Stable key identifying a login target — jobs with the same key share one
    // authenticated ApiClient / cookie jar. The headers are part of the key
    // because they determine WHICH tenant the session authenticates as (the
    // X-Honuware-Site header); without them, every tenant sharing one
    // origin/email/password would collapse into a single wrong session.
    static std::string TargetKey(
        const std::string& serverUrl,
        const std::string& serviceAccountEmail,
        const std::string& serviceAccountPassword,
        const std::vector<std::pair<std::string, std::string>>& headers);

    // Returns the ApiClient for the job's target, creating (but NOT logging in)
    // one bound to job.serverUrl on first use.
    ApiClientPtr EnsureClient(const ScheduledJob& job);

    Http::HttpClientPtr httpClient_;
    std::vector<ScheduledJob> jobs_;
    std::map<std::string, ApiClientPtr> clientsByTarget_;
    std::vector<std::unique_ptr<boost::asio::steady_timer>> timers_;
    bool started_ = false;
};

using JobSchedulerPtr = std::shared_ptr<JobScheduler>;

}  // namespace Scheduler
