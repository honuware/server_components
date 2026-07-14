#pragma once

#include <crow.h>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "endpoints/web_app_types.h"  // forward-declares WebApp (see header)

namespace Auth {

enum class CookieSameSitePolicy {
    Strict,
    Lax,
    None
};

struct CookieProperties {
    std::string domain;
    std::string path = "/";
    CookieSameSitePolicy sameSite = CookieSameSitePolicy::None;
    bool secure = false;
    bool httpOnly = false;
    int64_t maxAgeInMicros = -1;   // -1 means unset
};

class CookieManager {
public:
    CookieManager() = default;
    CookieManager(const CookieManager&) = delete;
    CookieManager& operator=(const CookieManager&) = delete;
    virtual ~CookieManager() = default;

    virtual void SetCookie(
        std::string_view key,
        std::string_view value,
        const CookieProperties& cookieProperties) = 0;

    virtual const std::unordered_map<std::string, std::string>& GetCookies() const = 0;
};

using CookieManagerPtr = std::shared_ptr<CookieManager>;

namespace Detail {

// Phase 6.2 of the security review: defensive check for dangerous
// cookie-attribute combinations. Returns a non-empty human-readable
// description of the problem when `props` is invalid; empty string
// otherwise.
//
// Currently flags exactly one case: SameSite=None requires Secure.
// Modern browsers (Chrome 80+, Firefox, Safari) outright REJECT a
// SameSite=None cookie that isn't also Secure — the cookie just
// silently disappears. We'd rather catch this at set time and log
// a loud error than ship a build that's silently logging users out.
//
// The production CookieManagerImpl::SetCookie consults this on
// every call. Tests call it directly to lock the contract.
std::string CheckCookieAttributes(
    std::string_view key, const CookieProperties& props);

}  // namespace Detail

class CookieManagerFactory {
public:
    virtual ~CookieManagerFactory() = default;
    virtual CookieManagerPtr CreateCookieManager(
        WebApp& webApp, const crow::request& req, crow::response& resp) = 0;
};

using CookieManagerFactoryPtr = std::shared_ptr<CookieManagerFactory>;
CookieManagerFactoryPtr MakeCookieManagerFactory();

// Factory
CookieManagerPtr MakeCookieManager(
    WebApp& webApp, const crow::request& req, crow::response& resp);

} // namespace Auth