#pragma once

#include "cookie_manager.h"

#include <unordered_map>

namespace Auth {
namespace Test {

class CookieManagerTest : public CookieManager {
public:
    CookieManagerTest() = default;
    CookieManagerTest(const CookieManagerTest&) = delete;
    CookieManagerTest& operator=(const CookieManagerTest&) = delete;
    ~CookieManagerTest() override = default;

    void SetCookie(
        std::string_view key,
        std::string_view value,
        const CookieProperties& cookieProperties) override;

    const std::unordered_map<std::string, std::string>& GetCookies() const override;

    const std::unordered_map<std::string, CookieProperties>& GetCookieProperties() const;

private:
    std::unordered_map<std::string, std::string> cookies_;
    std::unordered_map<std::string, CookieProperties> cookieProperties_;
};

using CookieManagerTestPtr = std::shared_ptr<CookieManagerTest>;
CookieManagerTestPtr MakeCookieManagerTest();

CookieManagerFactoryPtr MakeTestCookieManagerFactory(CookieManagerPtr cookieManager);

} // namespace Test
} // namespace Auth