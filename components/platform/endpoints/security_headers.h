#pragma once

#include <string>
#include <utility>

#include <crow/http_request.h>
#include <crow/http_response.h>

// Phase 7.1 of the security review: SecurityHeaders middleware.
//
// Sets a baseline of HTTP response headers that browsers consult for
// cross-cutting defenses — content-sniffing, referrer leaks, framing,
// etc. The middleware runs on EVERY response (after_handle), so it
// covers static endpoints, error responses, and any future routes.
//
// HSTS and CSP only emit in prod mode (`Auth::ServerConfig::IsProdMode()`).
// HSTS over plain HTTP is a footgun — once a browser sees the header
// it refuses to talk to the host over HTTP for the next year. Dev
// machines on http://localhost would lock themselves out. CSP is
// noisy on dev (Angular live-reload uses inline scripts, etc.) and
// is best tuned per real-deploy.
//
// X-Content-Type-Options, Referrer-Policy, Permissions-Policy, and the
// Server banner override are emitted in EVERY mode — they're cheap and
// have no dev-mode side effects.
//
// Test integration: SetEnabled(false) flips the middleware to a
// no-op for tests that don't want the headers polluting their JSON
// assertions. EndpointTestHelper turns this off by default for the
// same reason it turns CsrfGuard off — to avoid retrofitting 100+
// pre-existing endpoint tests. The dedicated security_headers_test.cpp
// runs with the middleware enabled.
namespace Endpoints {

class SecurityHeaders {
public:
    struct context {};

    SecurityHeaders();

    void before_handle(crow::request& req, crow::response& res, context& ctx);
    void after_handle(crow::request& req, crow::response& res, context& ctx);

    // Defaults to true. EndpointTestHelper flips this to false so
    // the per-endpoint tests don't have to assert on or strip the
    // security headers. The dedicated test re-enables it.
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

    // App-supplied `Server` header value. Empty (the default) means the
    // framework emits no Server header at all — it is brand-specific, so the
    // app opts in at startup via
    //   webApp.GetApp().get_middleware<SecurityHeaders>().SetServerBanner(...)
    // keeping the middleware brand-free for the honuware extraction (Phase 3.2).
    void SetServerBanner(std::string banner) { serverBanner_ = std::move(banner); }
    const std::string& GetServerBanner() const { return serverBanner_; }

private:
    bool enabled_ = true;
    std::string serverBanner_;
};

}  // namespace Endpoints
