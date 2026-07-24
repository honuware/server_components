#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>

#include "job_scheduler.h"
#include "scheduled_job.h"
#include "util/http/http_client.h"

namespace Scheduler {

// Supplies the current desired job set on demand. Called once at Initialize()
// and again on every refresh tick. This is how control-mode multi-tenancy stays
// out of the engine: the app hands over a callback that reads the control
// database's active tenants and builds the concatenated catalog, and the engine
// simply re-asks it periodically — it never learns what a tenant is.
using JobListProvider = std::function<std::vector<ScheduledJob>()>;

// Top-level orchestrator for the scheduler helper. Wires a JobScheduler and a
// single io_context together; installs SIGTERM/SIGINT handlers; runs the event
// loop until graceful shutdown.
//
// The job list is supplied by the caller (built app-side — knottyyoga's catalog
// lives next to its main.cpp), so the engine carries no knowledge of which
// endpoints exist or how often they run. Each ScheduledJob is self-describing
// (target URL + credentials + headers), which is what keeps this class fully
// app-agnostic.
//
// Lifecycle:
//   Scheduler s(std::move(jobs), MakeHttpClient());
//   if (!s.Initialize()) return 1;     // register + authenticate targets
//   s.Run();                            // blocks until signal
//
// Tests use RunFor(duration) instead of Run() so they can drive the
// io_context without installing process-wide signal handlers.
class Scheduler {
public:
    // Static job set — the single-tenant / fixed-mode path (today's behavior).
    Scheduler(std::vector<ScheduledJob> jobs, Http::HttpClientPtr httpClient);

    // Refreshing job set — the control-mode path. The provider is called at
    // Initialize() to build the initial set and re-called every refreshInterval
    // (from Run()) so newly-provisioned tenants are picked up and suspended
    // tenants' jobs stop, all without a process restart. A refreshInterval <= 0
    // disables the periodic refresh (the initial provider call still happens).
    Scheduler(
        JobListProvider provider,
        std::chrono::seconds refreshInterval,
        Http::HttpClientPtr httpClient);
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // Registers each supplied job, then logs in to every distinct target used
    // by an enabled job. Returns true on success, false if any target login
    // fails — callers (main) should exit non-zero so systemd retries the
    // container. When constructed with a provider, the initial job set is built
    // by calling it here (a throwing provider propagates out — main treats that
    // as a fatal startup error).
    bool Initialize();

    // Re-asks the provider for the current job set and, if it differs from the
    // running set, atomically reloads it: stops the timers, swaps the jobs,
    // re-authenticates the new set, and restarts the timers. Returns true when a
    // reload happened, false when there was no provider, the set was unchanged,
    // or the provider threw (logged and treated as "keep the current set" so a
    // transient control-DB blip never tears the scheduler down). Public so tests
    // can drive a refresh deterministically instead of waiting on the timer.
    bool Refresh();

    // Production path. Installs SIGINT/SIGTERM handlers, starts the timers,
    // and blocks on io_context::run() until a signal triggers Shutdown().
    // Calling Run() before Initialize() succeeds is a no-op (logs an error).
    void Run();

    // Test path. Drives the io_context for at most `duration`. Does NOT
    // install signal handlers. Safe to call multiple times — each call
    // re-arms the timers from a clean state.
    void RunFor(std::chrono::milliseconds duration);

    // Stops timers and asks the io_context to exit. Idempotent. Called
    // automatically from the destructor and from the signal handler.
    void Shutdown();

    // Inspection helper (primarily for tests).
    const JobScheduler& GetJobScheduler() const { return *jobScheduler_; }

private:
    void InstallSignalHandlers();

    // Arms the periodic refresh timer (control mode with a positive interval).
    void ScheduleRefresh();

    std::vector<ScheduledJob> jobs_;
    boost::asio::io_context ioContext_;
    std::unique_ptr<JobScheduler> jobScheduler_;
    std::unique_ptr<boost::asio::signal_set> signals_;
    bool initialized_ = false;

    // Control-mode refresh. provider_ is empty in the static/fixed path.
    JobListProvider provider_;
    std::chrono::seconds refreshInterval_{0};
    std::vector<ScheduledJob> lastJobs_;  // last provider() result, for change detection
    std::unique_ptr<boost::asio::steady_timer> refreshTimer_;
};

}  // namespace Scheduler
