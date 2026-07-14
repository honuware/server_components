#pragma once

#include <string>

#include <crow.h>

// Reverse-proxy awareness for the C++ server.
//
// The CloudFront-fronted EC2 deploy runs Crow on plain HTTP at port 80.
// Viewer ↔ CloudFront is HTTPS; CloudFront ↔ EC2 is HTTP. From Crow's
// perspective every request appears to come over HTTP from a CloudFront
// edge IP, which is *not* the real viewer's address or scheme.
//
// CloudFront forwards the original viewer scheme + IP via these headers:
//   X-Forwarded-Proto   "https" or "http"
//   X-Forwarded-For     "<original-viewer-ip>, <intermediate-1>, ..."
//
// Trusting these headers blindly would let any caller hitting the EC2 IP
// directly (bypassing CloudFront) spoof the viewer scheme/IP. Phase 1.7's
// CloudFrontOriginGuard middleware will 403 those direct-origin requests,
// but until then — and as a defense-in-depth thereafter — these helpers
// only consult the forwarded headers when the operator has explicitly
// opted in via the HONUWARE_TRUST_PROXY env var (legacy fallback:
// KNOTTYYOGA_TRUST_PROXY).
//
// Note on cookies: today's cookie code (session.cpp) sets `Secure` based
// on `ServerConfig::IsProdMode()`, not on the request scheme. So the
// `Secure` decision works correctly for the CloudFront deploy without
// consulting these helpers at all. The helpers exist for future callers
// that need the *viewer*'s scheme/IP — request logging, HSTS preload
// checks, abuse-detection by client IP, etc.
namespace Auth {

// Reads HONUWARE_TRUST_PROXY (legacy fallback: KNOTTYYOGA_TRUST_PROXY). True
// when the value is "1" or "true" (case-insensitive). Anything else (unset,
// "0", "false") is not trusted.
bool ProxyTrustEnabled();

// When the proxy is trusted and the request carries X-Forwarded-Proto,
// returns that header's value (typically "https" when CloudFront fronts
// the EC2). When not trusted or the header is missing, returns the empty
// string — indicating "we don't know what scheme the viewer used".
std::string ResolveViewerScheme(const crow::request& req);

// When the proxy is trusted and the request carries X-Forwarded-For,
// returns the first IP in the comma-separated list — the original viewer
// IP that CloudFront recorded. Subsequent entries are intermediate proxies
// and are dropped. When not trusted or the header is missing, returns the
// empty string.
std::string ResolveViewerIp(const crow::request& req);

// Phase 5.5 of the security review: best-effort client-IP resolution
// for the rate-limit / brute-force gate.
//
// Production (CloudFront → EC2): when HONUWARE_TRUST_PROXY is set,
// returns the X-Forwarded-For value (i.e. the real viewer IP CloudFront
// recorded). Local dev / test (no proxy): falls back to Crow's
// `req.remote_ip_address` (the connection peer address).
//
// Returns an empty string when neither source has a value — callers
// (the gate, the async recorder) MUST treat this as "couldn't
// determine an IP; skip per-IP logic" rather than as a sentinel.
// The gate's per-IP check explicitly skips on empty input so a
// production deploy with the trust env var off doesn't false-positive.
std::string ResolveClientIp(const crow::request& req);

}  // namespace Auth
