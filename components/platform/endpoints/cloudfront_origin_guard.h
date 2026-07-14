#pragma once

#include <chrono>
#include <mutex>
#include <string>

#include <crow/http_request.h>
#include <crow/http_response.h>

// Replaces nginx's "only let CloudFront talk to us" check.
//
// CloudFront is configured to add an "Origin custom header"
// `X-Origin-Secret: <random>` to every request it forwards to the EC2.
// This middleware compares that header to a process-startup-loaded secret
// from the env var `HONUWARE_ORIGIN_SECRET` (legacy fallback:
// `KNOTTYYOGA_ORIGIN_SECRET`). Requests that don't carry
// the matching header are rejected with HTTP 403 + a small JSON body.
//
// `/api/health` is allow-listed: AWS Synthetics canaries, the Phase 5.3
// uptime probes, and the helper-watchdog can reach the health endpoint
// without injecting CloudFront's secret.
//
// When the env var is unset (e.g., local dev), the guard is *disabled*
// and every request passes through. Operators see a single LogInfo line
// at startup announcing the guard's state so a misconfigured EC2 is
// obvious on first boot.
//
// Rejected requests log at most once per minute per process so a stray
// scanner can't flood the journal.
namespace Endpoints {

class CloudFrontOriginGuard {
public:
    // Crow middleware contract: an empty per-request context type.
    struct context {};

    // Reads `HONUWARE_ORIGIN_SECRET` (legacy fallback:
    // `KNOTTYYOGA_ORIGIN_SECRET`) once at construction. The Crow
    // App owns one instance for the process lifetime, so we only pay
    // the env-var lookup once.
    CloudFrontOriginGuard();

    // Crow middleware contract: invoked before every route handler.
    // When the guard rejects, calls `res.end()` so the handler is skipped.
    void before_handle(crow::request& req, crow::response& res, context& ctx);

    // Crow middleware contract: no-op for this guard.
    void after_handle(crow::request& req, crow::response& res, context& ctx);

    // True when the env var was set at construction. Public for tests
    // and for startup logging.
    bool IsActive() const { return active_; }

private:
    bool active_ = false;
    std::string expectedSecret_;

    // Throttle for rejection logs: at most one log line per minute
    // across the whole process, so a port scanner can't drown the
    // journal in 403 messages.
    std::mutex logMutex_;
    std::chrono::steady_clock::time_point lastRejectLog_{};
};

}  // namespace Endpoints
