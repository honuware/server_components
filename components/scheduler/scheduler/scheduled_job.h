#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Scheduler {

// Definition of a single recurring job. A job is *fully self-describing*: it
// carries not only what HTTP call to make and how often, but also which server
// to call, which service account to authenticate as, and what extra headers to
// attach. That self-sufficiency is what lets the engine stay app-agnostic — it
// runs whatever list of jobs it is handed and never learns what a "tenant" is.
// Multi-tenancy is then just a catalog whose jobs point at different
// servers/credentials, assembled entirely app-side (Phase 1.7 componentization,
// tenancy hand-off #3).
//
// Disabled jobs (interval <= 0) are skipped during timer setup.
struct ScheduledJob {
    std::string name;          // Display name, used in logs and lookups
    std::string method;        // "POST", "GET"
    std::string path;          // Path beginning with "/", e.g. "/api/admin/run_billing"
    int64_t intervalSeconds = 0;  // 0 disables the job

    // Target + credentials for this job. The engine groups registered jobs by
    // (serverUrl, serviceAccountEmail, serviceAccountPassword), logs in once
    // per distinct target, and reuses that session for every job sharing it.
    std::string serverUrl;              // Base URL, e.g. "http://localhost:80"
    std::string serviceAccountEmail;    // Login email for this job's target
    std::string serviceAccountPassword; // Login password for this job's target

    // Extra request headers attached to this job's HTTP call, applied on top of
    // the engine's Content-Type/Cookie handling (a header here overrides those).
    // Empty for knottyyoga today; the tenant plan uses it for headers like an
    // origin secret or a per-tenant site header.
    std::vector<std::pair<std::string, std::string>> headers;

    // Member-wise equality. Used by the control-mode tenant refresh to detect
    // whether the newly-built job list actually differs from the running one
    // before paying to reload + re-authenticate.
    friend bool operator==(const ScheduledJob&, const ScheduledJob&) = default;
};

// Outcome of a single job execution. Surfaced by RunJobOnce so the
// scheduler can log it and tests can assert on it.
struct JobExecutionResult {
    std::string jobName;
    int statusCode = 0;
    // True when the call returned a 2xx status. Network errors and 4xx/5xx
    // both surface as success=false.
    bool success = false;
    // Populated when the call threw (e.g. libcurl error).
    std::string errorMessage;
    std::chrono::milliseconds durationMs{0};
};

}  // namespace Scheduler
