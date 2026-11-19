#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

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
// require it on input. So are `/api/forgot_password` and
// `/api/reset_password`, which serve someone who can't sign in.
// Origin/Referer checks for those bootstrap endpoints are deferred to a
// follow-up.
//
// An app can exempt a route of its own the same way, with
// AllowWithoutToken, when it serves someone who isn't signed in and acts
// on no one's session: CommunityFinder's public photo-removal request is
// the first (CM3 Phase 2 §12). A forged request to such a route carries
// no authority the attacker doesn't already have by calling it directly.
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

    // Lets a state-changing request to this exact path (a query string
    // aside) through without a token, as the bootstrap paths are. For a
    // route that takes no session: it must not read or change anything
    // that belongs to a signed-in person. Call it from the route's
    // AddRoute, while the app is set up: the list isn't locked, so it must
    // not change once requests are being served. A path given twice is
    // kept once.
    void AllowWithoutToken(std::string_view path);
    bool IsAllowedWithoutToken(std::string_view url) const;

private:
    bool enabled_ = true;
    std::vector<std::string> allowedWithoutToken_;

    // Throttle for rejection logs: at most one log line per minute
    // across the whole process so a misbehaving client (or a probe)
    // can't drown the journal in 403 messages.
    std::mutex logMutex_;
    std::chrono::steady_clock::time_point lastRejectLog_{};
};

}  // namespace Endpoints
