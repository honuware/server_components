#include "cookie_manager_test_util.h"

namespace Auth {
namespace Test {

void CookieManagerTest::SetCookie(
    std::string_view key,
    std::string_view value,
    const CookieProperties& cookieProperties) {
    std::string k(key);
    cookies_[k] = std::string(value);
    cookieProperties_[k] = cookieProperties;
}

const std::unordered_map<std::string, std::string>& CookieManagerTest::GetCookies() const {
    return cookies_;
}

const std::unordered_map<std::string, CookieProperties>& CookieManagerTest::GetCookieProperties() const {
    return CookieManagerTest::cookieProperties_;
}

CookieManagerTestPtr MakeCookieManagerTest() {
    return std::make_shared<CookieManagerTest>();
}

class CookieManagerFactoryTest : public CookieManagerFactory {
public:
    CookieManagerFactoryTest(CookieManagerPtr cookieManager)
        : cookieManager_(cookieManager) {}   
    ~CookieManagerFactoryTest() override = default;
    CookieManagerPtr CreateCookieManager(
        WebApp&, const crow::request&, crow::response&) override {
        return cookieManager_;
    }

private:
    CookieManagerPtr cookieManager_;
};

CookieManagerFactoryPtr MakeTestCookieManagerFactory(CookieManagerPtr cookieManager) {
    return std::make_shared<CookieManagerFactoryTest>(cookieManager);
}

} // namespace Test
} // namespace Auth