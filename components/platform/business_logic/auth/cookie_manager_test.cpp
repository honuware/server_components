#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "cookie_manager.h"
#include "cookie_manager_test_util.h"

namespace Auth {
namespace Test {
namespace {

TEST(CookieManagerUtilTest, SetCookieBasic) {
    auto mgr = MakeCookieManagerTest();

    CookieProperties props;
    props.domain = "example.com";
    props.path = "/";
    props.sameSite = CookieSameSitePolicy::Lax;
    props.secure = true;
    props.httpOnly = true;
    props.maxAgeInMicros = 5LL * 60LL * 1000000LL;

    mgr->SetCookie("session", "abc123", props);

    auto cookies = mgr->GetCookies();
    ASSERT_EQ(cookies.size(), 1u);
    EXPECT_EQ(cookies.at("session"), "abc123");

    auto propsMap = mgr->GetCookieProperties();
    ASSERT_EQ(propsMap.size(), 1u);
    auto it = propsMap.find("session");
    ASSERT_NE(it, propsMap.end());
    EXPECT_EQ(it->second.domain, "example.com");
    EXPECT_EQ(it->second.path, "/");
    EXPECT_EQ(it->second.sameSite, CookieSameSitePolicy::Lax);
    EXPECT_TRUE(it->second.secure);
    EXPECT_TRUE(it->second.httpOnly);
    EXPECT_EQ(it->second.maxAgeInMicros, 5LL * 60LL * 1000000LL);
}

TEST(CookieManagerUtilTest, GetCookiesBasic) {
    auto mgr = MakeCookieManagerTest();

    CookieProperties p1;
    mgr->SetCookie("k1", "v1", p1);
    CookieProperties p2;
    mgr->SetCookie("k2", "v2", p2);

    auto cookies = mgr->GetCookies();
    ASSERT_EQ(cookies.size(), 2u);
    EXPECT_EQ(cookies.at("k1"), "v1");
    EXPECT_EQ(cookies.at("k2"), "v2");
}

// =====================================================================
// Phase 6.2 of the security review: defensive attribute check.
//
// CheckCookieAttributes returns a non-empty diagnostic string for
// the one combination modern browsers will silently reject:
// SameSite=None without Secure. Anything else (Lax, Strict,
// None+Secure) returns empty. The production CookieManagerImpl
// logs this string to LogError before setting the cookie — we
// don't fail the call, just make the bad config impossible to
// miss in the journal.
// =====================================================================

TEST(CookieAttributesCheckTest, SameSiteNoneWithoutSecureIsFlagged) {
    CookieProperties props;
    props.sameSite = CookieSameSitePolicy::None;
    props.secure = false;

    std::string warning = Detail::CheckCookieAttributes("session_token", props);
    EXPECT_FALSE(warning.empty());
    // The diagnostic should include the cookie name so the operator
    // can grep for "which cookie was misconfigured."
    EXPECT_NE(warning.find("session_token"), std::string::npos);
    // And mention SameSite=None / Secure so the fix is obvious.
    EXPECT_NE(warning.find("SameSite=None"), std::string::npos);
    EXPECT_NE(warning.find("Secure"), std::string::npos);
}

TEST(CookieAttributesCheckTest, SameSiteNoneWithSecureIsAccepted) {
    // The legitimate cross-origin case. Don't false-positive.
    CookieProperties props;
    props.sameSite = CookieSameSitePolicy::None;
    props.secure = true;

    EXPECT_EQ(Detail::CheckCookieAttributes("anything", props), "");
}

TEST(CookieAttributesCheckTest, SameSiteLaxAcceptsAnySecureValue) {
    // Lax is the project's default and works on plain HTTP, so
    // Secure=false must NOT trigger the warning.
    CookieProperties laxNotSecure;
    laxNotSecure.sameSite = CookieSameSitePolicy::Lax;
    laxNotSecure.secure = false;
    EXPECT_EQ(Detail::CheckCookieAttributes("k", laxNotSecure), "");

    CookieProperties laxSecure;
    laxSecure.sameSite = CookieSameSitePolicy::Lax;
    laxSecure.secure = true;
    EXPECT_EQ(Detail::CheckCookieAttributes("k", laxSecure), "");
}

TEST(CookieAttributesCheckTest, SameSiteStrictAcceptsAnySecureValue) {
    CookieProperties strictNotSecure;
    strictNotSecure.sameSite = CookieSameSitePolicy::Strict;
    strictNotSecure.secure = false;
    EXPECT_EQ(Detail::CheckCookieAttributes("k", strictNotSecure), "");

    CookieProperties strictSecure;
    strictSecure.sameSite = CookieSameSitePolicy::Strict;
    strictSecure.secure = true;
    EXPECT_EQ(Detail::CheckCookieAttributes("k", strictSecure), "");
}

// HttpOnly / Path / Domain / Max-Age don't influence the check —
// only SameSite + Secure do. Lock that so a future expansion of
// the function (for example, adding a Path validation) doesn't
// silently change the cookie-setting behavior.
TEST(CookieAttributesCheckTest, OtherAttributesDoNotInfluenceTheCheck) {
    CookieProperties props;
    props.sameSite = CookieSameSitePolicy::Lax;  // safe
    props.secure = false;
    props.httpOnly = true;
    props.path = "/some/path";
    props.domain = "example.com";
    props.maxAgeInMicros = 12345;
    EXPECT_EQ(Detail::CheckCookieAttributes("k", props), "");

    // Same attributes but with the dangerous SameSite/Secure combo:
    // the unrelated fields don't suppress the warning.
    props.sameSite = CookieSameSitePolicy::None;
    EXPECT_FALSE(Detail::CheckCookieAttributes("k", props).empty());
}

} // namespace
} // namespace Test
} // namespace Auth