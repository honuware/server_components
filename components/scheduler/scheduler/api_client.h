#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/http/http_client.h"

namespace Scheduler {

// Authenticated HTTP client for calling the web server's admin endpoints.
//
// Wraps Http::HttpClient and adds session-cookie semantics:
//   - Login() caches the credentials and parses Set-Cookie from the response.
//   - CallEndpoint() attaches the stored cookies via a Cookie: header.
//   - On 401, CallEndpoint() clears the cookies, re-invokes Login() with the
//     cached credentials, and retries the original request once.
//
// Cookie parsing here is intentionally minimal (single origin, no full RFC
// 6265): we extract name=value before the first ';' from each Set-Cookie
// header and store name → value. That's enough for the helper, which only
// ever talks to one origin and only needs the session_token cookie.
class ApiClient {
public:
    // loginHeaders are attached to the POST /api/login request (and to every
    // re-login on the 401-retry path). Multi-tenancy needs this: in control mode
    // the server resolves which tenant database to authenticate against from the
    // X-Honuware-Site request header, and the tenant-resolution guard rejects a
    // header-less /api/login. Each per-tenant ApiClient therefore logs in with
    // that tenant's site header (plus any deployment-wide header such as the
    // origin secret). Empty for the single-tenant deployment — then Login behaves
    // exactly as before.
    ApiClient(
        Http::HttpClientPtr httpClient,
        std::string baseUrl,
        std::vector<std::pair<std::string, std::string>> loginHeaders = {});

    // Authenticates against POST /api/login. On 200, parses Set-Cookie
    // headers from the response, stores name=value pairs internally, and
    // caches the credentials for use in CallEndpoint's 401-retry path. The
    // client's loginHeaders (see the constructor) are attached to the request.
    // Returns true on a 200 response, false otherwise.
    bool Login(std::string_view email, std::string_view password);

    // Performs an authenticated HTTP call to baseUrl + path. Attaches a
    // Cookie: header built from the stored cookies. If the response is 401
    // and credentials have been cached, clears the cookies, calls Login()
    // again, and retries the original request once. The retried response
    // is returned regardless of status — the caller decides what to do.
    //
    // method:       "GET", "POST", "PUT", "DELETE", "PATCH"
    // path:         path beginning with "/", e.g. "/api/admin/run_billing"
    // body:         request body (sent as-is); should be JSON for POST/PUT/PATCH
    // extraHeaders: additional request headers applied on top of the engine's
    //               Content-Type/Cookie handling. A header named here overrides
    //               an engine-set one of the same name. Carried across the
    //               401-retry so the retried request is identical.
    Http::HttpResponse CallEndpoint(
        std::string_view method,
        std::string_view path,
        std::string_view body = "",
        const std::vector<std::pair<std::string, std::string>>& extraHeaders = {});

    // Inspection helpers — primarily for tests.
    size_t GetCookieCount() const { return cookies_.size(); }
    bool HasCookie(std::string_view name) const;
    std::string GetCookieValue(std::string_view name) const;

private:
    // Builds an absolute URL by joining baseUrl_ and the supplied path,
    // trimming any redundant slash between them.
    std::string BuildUrl(std::string_view path) const;

    // Parses a Set-Cookie header value (name=value plus optional attributes)
    // and stores name → value. Anything after the first ';' is discarded.
    void StoreSetCookie(std::string_view setCookieValue);

    // Builds the Cookie: header value from cookies_, e.g. "k1=v1; k2=v2".
    // Returns an empty string when no cookies are stored.
    std::string BuildCookieHeader() const;

    // Reads "Set-Cookie" / "set-cookie" from response headers and stores
    // each entry. The current HttpClient flattens duplicate header keys
    // into a single map entry, so in practice only the last Set-Cookie
    // survives — fine for the helper, which only needs session_token.
    void HarvestSetCookies(const Http::HttpResponse& response);

    // Performs a single Execute() with cookies attached and request
    // body/method/headers populated. No retry semantics.
    Http::HttpResponse ExecuteOnce(
        std::string_view method,
        std::string_view path,
        std::string_view body,
        const std::vector<std::pair<std::string, std::string>>& extraHeaders) const;

    Http::HttpClientPtr httpClient_;
    std::string baseUrl_;
    std::string email_;
    std::string password_;
    // Attached to every /api/login request (initial + 401-retry re-login).
    std::vector<std::pair<std::string, std::string>> loginHeaders_;
    std::map<std::string, std::string> cookies_;
};

using ApiClientPtr = std::shared_ptr<ApiClient>;

}  // namespace Scheduler
