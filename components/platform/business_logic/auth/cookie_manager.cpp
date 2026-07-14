#include "cookie_manager.h"

#include <crow.h>
#include <algorithm>
#include <sstream>

#include "endpoints/web_app.h"  // full WebApp definition (GetApp())
#include "util/logging.h"

namespace Auth {

namespace Detail {

std::string CheckCookieAttributes(
    std::string_view key, const CookieProperties& props) {
    if (props.sameSite == CookieSameSitePolicy::None && !props.secure) {
        std::stringstream ss;
        ss << "cookie '" << key << "' has SameSite=None but Secure=false; "
           << "modern browsers will reject this cookie outright. Either "
           << "add Secure (HTTPS-only) or change SameSite to Lax/Strict.";
        return ss.str();
    }
    return "";
}

}  // namespace Detail

namespace {

class CookieManagerImpl : public CookieManager {
public:
    CookieManagerImpl(WebApp& webApp, const crow::request& req, crow::response& resp)
        : webApp_(webApp), req_(req), resp_(resp) {}

    void SetCookie(
        std::string_view key,
        std::string_view value,
        const CookieProperties& cookieProperties) override {
        // Phase 6.2 of the security review: warn loudly when a
        // cookie ships with attributes the browser will silently
        // reject. We still set the cookie (the helper is advisory)
        // so behavior doesn't change, but the operator gets a log
        // line they can grep for.
        std::string warning =
            Detail::CheckCookieAttributes(key, cookieProperties);
        if (!warning.empty()) {
            LogError() << "[cookie_manager] event=invalid_attributes "
                       << warning << "\n";
        }

        auto& cookieContext = webApp_.GetApp().get_context<crow::CookieParser>(req_);
        auto& cookie = cookieContext.set_cookie(std::string(key), std::string(value));

        // Only set attributes if they have meaningful values.
        if (!cookieProperties.path.empty()) {
            cookie.path(cookieProperties.path);
        }
        if (!cookieProperties.domain.empty()) {
            cookie.domain(cookieProperties.domain);
        }
        switch(cookieProperties.sameSite) {
            case CookieSameSitePolicy::Strict:
                cookie.same_site(
                    crow::CookieParser::Cookie::SameSitePolicy::Strict);
                break;
            case CookieSameSitePolicy::Lax:
                cookie.same_site(
                    crow::CookieParser::Cookie::SameSitePolicy::Lax);
                break;
            case CookieSameSitePolicy::None:
                cookie.same_site(
                    crow::CookieParser::Cookie::SameSitePolicy::None);
                break;
            default:
                break;
        }
        if (cookieProperties.maxAgeInMicros >= 0) {
            // Crow max_age is in seconds.
            long long seconds =
                static_cast<long long>(cookieProperties.maxAgeInMicros / 1000000LL);
            cookie.max_age(seconds);
        }
        if (cookieProperties.secure) {
            cookie.secure();
        }
        if (cookieProperties.httpOnly) {
            cookie.httponly();
        }
    }

    const std::unordered_map<std::string, std::string>& GetCookies() const override {
        auto& cookieContext = webApp_.GetApp().get_context<crow::CookieParser>(req_);
        if(cookieContext.jar.size() != cookies_.size()) {
            for (const auto& [key, value] : cookieContext.jar) {
                cookies_[key] = value;
            }
        }
        return cookies_;
    }

private:
    WebApp& webApp_;
    const crow::request& req_;
    crow::response& resp_;
    // This is a readonly cache of cookies parsed from the request.
    mutable std::unordered_map<std::string, std::string> cookies_;
};

} // namespace

CookieManagerPtr MakeCookieManager(WebApp& webApp, const crow::request& req, crow::response& resp) {
    return std::make_shared<CookieManagerImpl>(webApp, req, resp);
}

class CookieManagerFactoryImpl : public CookieManagerFactory {
public:
    ~CookieManagerFactoryImpl() override = default;
    CookieManagerPtr CreateCookieManager(
        WebApp& webApp, const crow::request& req, crow::response& resp) override {
        return MakeCookieManager(webApp, req, resp);
    }
};

CookieManagerFactoryPtr MakeCookieManagerFactory() {
    return std::make_shared<CookieManagerFactoryImpl>();
}

} // namespace Auth