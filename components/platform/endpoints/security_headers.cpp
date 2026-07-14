#include "security_headers.h"

#include <string_view>

#include "business_logic/auth/server_config.h"

namespace Endpoints {
namespace {

// Header values are constants so the production overhead is one
// pointer copy per response, no string formatting.

// One year, with subdomain coverage and preload-list eligibility.
// Browsers refuse to talk to the host over plain HTTP for max-age
// seconds after the FIRST visit — irreversible from the server side
// for that long, which is why we only emit in prod mode.
constexpr std::string_view kHsts =
    "max-age=31536000; includeSubDomains; preload";

// Tight default CSP. The actual deploy will likely need to broaden
// this for inline styles, embedded fonts, or analytics — that's a
// per-deploy tuning step, not a change to the middleware.
//   - default-src 'self': everything must come from our origin
//   - script-src 'self': no inline / no third-party JS
//   - style-src 'self' 'unsafe-inline': Angular Material relies on
//     inline styles; tighten when feasible
//   - img-src 'self' data:: data: covers inline base64 images the
//     SPA renders (avatars, etc.)
//   - connect-src 'self': fetch/XHR same-origin only
//   - frame-ancestors 'none': nobody embeds us in an iframe
//   - base-uri 'none': can't change the document base
constexpr std::string_view kCsp =
    "default-src 'self'; "
    "script-src 'self'; "
    "style-src 'self' 'unsafe-inline'; "
    "img-src 'self' data:; "
    "connect-src 'self'; "
    "frame-ancestors 'none'; "
    "base-uri 'none'";

// Always-on baseline. Cheap, no dev side effects.
constexpr std::string_view kXContentTypeOptions = "nosniff";
constexpr std::string_view kReferrerPolicy = "strict-origin-when-cross-origin";
constexpr std::string_view kPermissionsPolicy =
    "camera=(), microphone=(), geolocation=()";

}  // namespace

SecurityHeaders::SecurityHeaders() = default;

void SecurityHeaders::before_handle(
    crow::request& /*req*/,
    crow::response& /*res*/,
    context& /*ctx*/) {
    // No-op. We add headers in after_handle so we cover handler-set
    // responses, error responses, and any other path that produced
    // the response object.
}

void SecurityHeaders::after_handle(
    crow::request& /*req*/,
    crow::response& res,
    context& /*ctx*/) {
    if (!enabled_) return;

    // Always-on headers.
    res.set_header("X-Content-Type-Options", std::string(kXContentTypeOptions));
    res.set_header("Referrer-Policy", std::string(kReferrerPolicy));
    res.set_header("Permissions-Policy", std::string(kPermissionsPolicy));
    // The Server banner is brand-specific: emit it only when the app has opted
    // in via SetServerBanner(...). Empty (framework default) means no header.
    if (!serverBanner_.empty()) {
        res.set_header("Server", serverBanner_);
    }
    // Phase 7.2 of the security review: tell shared caches that the
    // response varies by Origin so a CORS response cached for one
    // origin doesn't get served to a request from a different origin.
    // Crow's CORSHandler does NOT add this header itself. Always-on
    // even on non-CORS responses — the header is harmless, and we'd
    // rather be wrong-by-having-it than wrong-by-not.
    res.set_header("Vary", "Origin");

    // Prod-only: HSTS + CSP. Skip in test/dev so:
    //  - http://localhost dev work doesn't lock the browser into
    //    HTTPS-only mode for 1 year
    //  - tests don't have to special-case CSP-blocked inline scripts
    //    when running headless / in a sandbox
    bool isProd = false;
    try {
        isProd = Auth::ServerConfig::GetInstance().IsProdMode();
    } catch (const std::exception&) {
        // ServerConfig isn't initialized — early-startup paths and
        // some test fixtures hit this. Treat as "not prod" so we
        // don't emit prod-only headers.
        isProd = false;
    }

    if (isProd) {
        res.set_header("Strict-Transport-Security", std::string(kHsts));
        res.set_header("Content-Security-Policy", std::string(kCsp));
    }
}

}  // namespace Endpoints
