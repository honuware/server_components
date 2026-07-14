#include "csrf_guard.h"

#include <array>
#include <string>
#include <string_view>

#include <crow/common.h>

#include "business_logic/auth/auth_helper.h"
#include "util/logging.h"

namespace Endpoints {
namespace {

constexpr std::string_view kCsrfCookieName = "csrft";
constexpr std::string_view kCsrfHeaderName = "X-CSRF-Token";

// Bootstrap endpoints that set the csrft cookie on the response.
// They cannot require it on the request — there is no prior session.
constexpr std::array<std::string_view, 4> kBootstrapPaths = {
    "/api/login",
    "/api/register",
    "/api/remember",
    "/api/verify",
};

constexpr std::chrono::minutes kRejectLogThrottle{1};

constexpr std::string_view kForbiddenBody =
    R"({"error":"csrf_token_missing_or_invalid"})";

// Strip the query-string portion of a URL so the bootstrap allow-list
// can match the path exactly. Some Crow versions hand `req.url`
// without the query, but defending here keeps the allow-list robust.
std::string_view PathOnly(std::string_view url) {
    size_t q = url.find('?');
    if (q == std::string_view::npos) return url;
    return url.substr(0, q);
}

bool IsBootstrap(std::string_view url) {
    std::string_view path = PathOnly(url);
    for (auto p : kBootstrapPaths) {
        if (path == p) return true;
    }
    return false;
}

bool IsStateChanging(crow::HTTPMethod m) {
    // Windows pulls in `<windows.h>` (via libpqxx) which `#define DELETE`,
    // and Crow responds by omitting the entire SCREAMING_CASE half of
    // its HTTPMethod enum (POST/PUT/PATCH/etc.) Use the PascalCase
    // names that are always defined regardless of platform.
    return m == crow::HTTPMethod::Post
        || m == crow::HTTPMethod::Put
        || m == crow::HTTPMethod::Patch
        || m == crow::HTTPMethod::Delete;
}

// Trim leading and trailing ASCII whitespace.
std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

// Pull a single named cookie out of a `Cookie:` header value. Cookies
// are joined with `; `; pairs are `name=value`. Returns "" when the
// named cookie isn't present.
std::string GetCookieValue(std::string_view header, std::string_view name) {
    size_t pos = 0;
    while (pos <= header.size()) {
        size_t semi = header.find(';', pos);
        std::string_view pair = header.substr(
            pos,
            semi == std::string_view::npos ? std::string_view::npos : semi - pos);
        pair = Trim(pair);
        size_t eq = pair.find('=');
        if (eq != std::string_view::npos) {
            std::string_view key = Trim(pair.substr(0, eq));
            if (key == name) {
                return std::string(Trim(pair.substr(eq + 1)));
            }
        }
        if (semi == std::string_view::npos) break;
        pos = semi + 1;
    }
    return "";
}

}  // namespace

CsrfGuard::CsrfGuard() = default;

void CsrfGuard::before_handle(
    crow::request& req,
    crow::response& res,
    context& /*ctx*/) {
    if (!enabled_) return;
    if (!IsStateChanging(req.method)) return;
    if (IsBootstrap(req.url)) return;

    std::string cookieHeader = req.get_header_value("Cookie");
    std::string csrft = GetCookieValue(cookieHeader, kCsrfCookieName);
    std::string presented = req.get_header_value(std::string(kCsrfHeaderName));

    bool ok = !csrft.empty()
        && !presented.empty()
        && Auth::AuthHelper::ConstantTimeEqual(csrft, presented);
    if (ok) return;

    {
        std::lock_guard<std::mutex> lock(logMutex_);
        auto now = std::chrono::steady_clock::now();
        if (lastRejectLog_.time_since_epoch().count() == 0
            || now - lastRejectLog_ >= kRejectLogThrottle) {
            lastRejectLog_ = now;
            const char* reason = csrft.empty()
                ? "csrft cookie missing"
                : (presented.empty()
                    ? "X-CSRF-Token header missing"
                    : "csrft cookie / X-CSRF-Token header mismatch");
            LogWarning()
                << "CsrfGuard rejected request: "
                << crow::method_name(req.method) << " "
                << PathOnly(req.url) << " ("
                << reason << "; further rejections suppressed for "
                << kRejectLogThrottle.count() << " min)\n";
        }
    }

    res.code = 403;
    res.set_header("Content-Type", "application/json");
    res.write(std::string(kForbiddenBody));
    res.end();
}

void CsrfGuard::after_handle(
    crow::request& /*req*/,
    crow::response& /*res*/,
    context& /*ctx*/) {
}

}  // namespace Endpoints
