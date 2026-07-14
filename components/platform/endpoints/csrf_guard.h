#pragma once

#include <chrono>
#include <mutex>

#include <crow/http_request.h>
#include <crow/http_response.h>

// Phase 4.2 of the security review: server-side CSRF middleware.
//
// On state-changing methods (POST, PUT, PATCH, DELETE), require the
// request to carry an `X-CSRF-Token` header whose value matches the
// `csrft` cookie. The cookie is set at login time by Session
// (Phase 4.1); the SPA reads it via document.cookie and echoes it as
// the header (Phase 4.3). A cross-origin attacker can't forge this
// because:
//   - The csrft cookie is SameSite=Lax, so cross-origin POST forms
//     don't carry it (modern browsers).
//   - Even when they do carry it, the attacker can't read the value
//     across origins, so they can't put a matching X-CSRF-Token on
//     their forged request (browsers block custom headers on simple
//     cross-origin requests via CORS preflight).
//
// Bootstrap endpoints (`/api/login`, `/api/register`, `/api/remember`,
// `/api/verify`) are exempt — they SET the cookie, so they cannot
// require it on input. Origin/Referer checks for those bootstrap
// endpoints are deferred to a follow-up.
//
// Test integration: production WebApp leaves the guard enabled; the
// test fixture EndpointTestHelper calls `SetEnabled(false)` after
// construction so existing endpoint tests can keep posting without
// also wiring up CSRF. A dedicated csrf_guard_test.cpp covers the
// enforcement behavior with the guard enabled.
namespace Endpoints {

class CsrfGuard {
public:
    struct context {};

    CsrfGuard();

    void before_handle(crow::request& req, crow::response& res, context& ctx);
    void after_handle(crow::request& req, crow::response& res, context& ctx);

    // Defaults to true. EndpointTestHelper flips this to false so the
    // 80+ endpoint tests that already simulate logged-in sessions
    // don't all need to also synthesize a matching CSRF token. The
    // dedicated csrf_guard_test.cpp re-enables it before each test.
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

private:
    bool enabled_ = true;

    // Throttle for rejection logs: at most one log line per minute
    // across the whole process so a misbehaving client (or a probe)
    // can't drown the journal in 403 messages.
    std::mutex logMutex_;
    std::chrono::steady_clock::time_point lastRejectLog_{};
};

}  // namespace Endpoints
