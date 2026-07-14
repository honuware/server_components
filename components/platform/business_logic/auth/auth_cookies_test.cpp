#include "auth_cookies.h"

#include <gtest/gtest.h>

#include "cookie_manager_test_util.h"
#include "endpoints/endpoint_test_helper.h"
#include "server_config.h"
#include "test/src/util/database_test_helper.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"

// Phase 6.1 of the security review.
//
// Tests cover the canonical attribute factory + the clear helper.
// We exercise both modes (test/dev and prod), both cookie shapes
// (HttpOnly=true session-style and HttpOnly=false csrft-style),
// and the contract that the clear-time call matches the set-time
// attributes byte-for-byte except for Max-Age.

namespace Auth {
namespace {

TEST(AuthCookiesTest, BuildAuthCookiePropertiesTestModeHttpOnly) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "BuildAuthCookiePropertiesTestModeHttpOnly",
        [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();

        CookieProperties props = BuildAuthCookieProperties(
            tx, secrets, /*isProd=*/false,
            /*httpOnly=*/true, /*maxAgeMicros=*/12345);

        EXPECT_EQ(props.path, "/");
        EXPECT_EQ(props.sameSite, CookieSameSitePolicy::Lax);
        EXPECT_TRUE(props.httpOnly);
        EXPECT_FALSE(props.secure)
            << "test/dev mode must not require HTTPS";
        EXPECT_TRUE(props.domain.empty())
            << "test/dev mode leaves Domain unset";
        EXPECT_EQ(props.maxAgeInMicros, 12345);
    });

    ServerConfig::Shutdown();
}

TEST(AuthCookiesTest, BuildAuthCookiePropertiesTestModeNotHttpOnly) {
    // csrft variant: HttpOnly must be false so the SPA can read it.
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "BuildAuthCookiePropertiesTestModeNotHttpOnly",
        [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();

        CookieProperties props = BuildAuthCookieProperties(
            tx, secrets, /*isProd=*/false,
            /*httpOnly=*/false, /*maxAgeMicros=*/0);

        EXPECT_FALSE(props.httpOnly)
            << "csrft cookie must be SPA-readable";
        EXPECT_EQ(props.sameSite, CookieSameSitePolicy::Lax);
        EXPECT_EQ(props.path, "/");
        EXPECT_EQ(props.maxAgeInMicros, 0);
    });

    ServerConfig::Shutdown();
}

TEST(AuthCookiesTest, BuildAuthCookiePropertiesProdModeHasSecureAndDomain) {
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "BuildAuthCookiePropertiesProdModeHasSecureAndDomain",
        [&](Transaction& tx) {
        // Initialize ServerConfig in prod mode via the standard
        // EndpointTestHelper path — same shape that
        // session_test.cpp uses.
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        secrets->AddSecret(tx, Secrets::kWebsiteAddress, "knottyyoga.example");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());
        ASSERT_TRUE(ServerConfig::GetInstance().IsProdMode());

        CookieProperties props = BuildAuthCookieProperties(
            tx, secrets, /*isProd=*/true,
            /*httpOnly=*/true, /*maxAgeMicros=*/123);

        EXPECT_TRUE(props.secure)
            << "prod mode must mark cookies Secure (HTTPS-only)";
        EXPECT_EQ(props.domain, "knottyyoga.example");
        EXPECT_EQ(props.sameSite, CookieSameSitePolicy::Lax)
            << "Same-origin behind CloudFront uses Lax, not None";
    });

    ServerConfig::Shutdown();
}

// ---- ClearAuthCookies ----

TEST(AuthCookiesTest, ClearAuthCookiesEmitsAllThreeCookies) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ClearAuthCookiesEmitsAllThreeCookies",
        [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        auto cookieMgr = ::Auth::Test::MakeCookieManagerTest();

        ClearAuthCookies(tx, secrets, cookieMgr);

        auto cookies = cookieMgr->GetCookies();
        ASSERT_NE(cookies.find("session_token"), cookies.end())
            << "session_token must be cleared";
        ASSERT_NE(cookies.find("device_token"), cookies.end())
            << "device_token must be cleared";
        ASSERT_NE(cookies.find("csrft"), cookies.end())
            << "csrft must be cleared";

        // All three cleared with empty value.
        EXPECT_TRUE(cookies.at("session_token").empty());
        EXPECT_TRUE(cookies.at("device_token").empty());
        EXPECT_TRUE(cookies.at("csrft").empty());
    });

    ServerConfig::Shutdown();
}

TEST(AuthCookiesTest, ClearAuthCookiesUsesMaxAgeZero) {
    // The whole point of "clearing" a cookie is Max-Age=0 — that's
    // what tells the browser to drop it. Pin the contract.
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ClearAuthCookiesUsesMaxAgeZero",
        [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        auto cookieMgr = ::Auth::Test::MakeCookieManagerTest();

        ClearAuthCookies(tx, secrets, cookieMgr);

        auto props = cookieMgr->GetCookieProperties();
        EXPECT_EQ(props.at("session_token").maxAgeInMicros, 0);
        EXPECT_EQ(props.at("device_token").maxAgeInMicros, 0);
        EXPECT_EQ(props.at("csrft").maxAgeInMicros, 0);
    });

    ServerConfig::Shutdown();
}

TEST(AuthCookiesTest, ClearAuthCookiesUsesSameSiteLax) {
    // The bug Phase 6.1 fixes: the previous logout.cpp used
    // SameSite=None at clear-time even though session.cpp set them
    // SameSite=Lax. Mismatch → browsers don't apply the deletion.
    // Lock the corrected behavior.
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ClearAuthCookiesUsesSameSiteLax",
        [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        auto cookieMgr = ::Auth::Test::MakeCookieManagerTest();

        ClearAuthCookies(tx, secrets, cookieMgr);

        auto props = cookieMgr->GetCookieProperties();
        EXPECT_EQ(props.at("session_token").sameSite,
                  CookieSameSitePolicy::Lax);
        EXPECT_EQ(props.at("device_token").sameSite,
                  CookieSameSitePolicy::Lax);
        EXPECT_EQ(props.at("csrft").sameSite,
                  CookieSameSitePolicy::Lax);
    });

    ServerConfig::Shutdown();
}

TEST(AuthCookiesTest, ClearAuthCookiesPreservesHttpOnlyDistinction) {
    // session_token / device_token must be HttpOnly (so script
    // can't read the bearer token). csrft must NOT be HttpOnly
    // (the SPA reads it for the X-CSRF-Token header). The clear
    // call has to mirror that, otherwise the Set-Cookie header
    // describing the deletion disagrees with the live cookie's
    // attributes — same browser-rejects-the-delete trap.
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ClearAuthCookiesPreservesHttpOnlyDistinction",
        [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        auto cookieMgr = ::Auth::Test::MakeCookieManagerTest();

        ClearAuthCookies(tx, secrets, cookieMgr);

        auto props = cookieMgr->GetCookieProperties();
        EXPECT_TRUE(props.at("session_token").httpOnly);
        EXPECT_TRUE(props.at("device_token").httpOnly);
        EXPECT_FALSE(props.at("csrft").httpOnly)
            << "csrft must remain SPA-readable on the clear path too";
    });

    ServerConfig::Shutdown();
}

TEST(AuthCookiesTest, ClearAuthCookiesProdModeHasSecureAndDomain) {
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ClearAuthCookiesProdModeHasSecureAndDomain",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        secrets->AddSecret(tx, Secrets::kWebsiteAddress, "knottyyoga.example");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());
        ASSERT_TRUE(ServerConfig::GetInstance().IsProdMode());

        auto cookieMgr = ::Auth::Test::MakeCookieManagerTest();
        ClearAuthCookies(tx, secrets, cookieMgr);

        auto props = cookieMgr->GetCookieProperties();
        for (const auto& name : {"session_token", "device_token", "csrft"}) {
            EXPECT_TRUE(props.at(name).secure)
                << "prod-mode clear for " << name << " must be Secure";
            EXPECT_EQ(props.at(name).domain, "knottyyoga.example")
                << "prod-mode clear for " << name << " must include Domain";
        }
    });

    ServerConfig::Shutdown();
}

TEST(AuthCookiesTest, ClearAuthCookiesNullCookieManagerIsNoop) {
    // Defensive: logout.cpp may receive a null cookie manager via
    // an early-error path. Don't crash.
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ClearAuthCookiesNullCookieManagerIsNoop",
        [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        EXPECT_NO_THROW(ClearAuthCookies(tx, secrets, nullptr));
    });

    ServerConfig::Shutdown();
}

// Set-time and clear-time must produce identical attributes
// (modulo Max-Age, which differs by definition). This is the
// load-bearing invariant Phase 6.1 exists to enforce.
TEST(AuthCookiesTest, SetAndClearAttributesMatchExceptForMaxAge) {
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "SetAndClearAttributesMatchExceptForMaxAge",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        secrets->AddSecret(tx, Secrets::kWebsiteAddress, "knottyyoga.example");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        // Session/device-style: HttpOnly=true, Max-Age = 30 minutes
        // for set, 0 for clear.
        const int64_t setMaxAge = 30LL * 60LL * 1000000LL;
        CookieProperties setProps = BuildAuthCookieProperties(
            tx, secrets, /*isProd=*/true,
            /*httpOnly=*/true, /*maxAgeMicros=*/setMaxAge);
        CookieProperties clearProps = BuildAuthCookieProperties(
            tx, secrets, /*isProd=*/true,
            /*httpOnly=*/true, /*maxAgeMicros=*/0);

        EXPECT_EQ(setProps.path, clearProps.path);
        EXPECT_EQ(setProps.domain, clearProps.domain);
        EXPECT_EQ(setProps.sameSite, clearProps.sameSite);
        EXPECT_EQ(setProps.secure, clearProps.secure);
        EXPECT_EQ(setProps.httpOnly, clearProps.httpOnly);
        EXPECT_NE(setProps.maxAgeInMicros, clearProps.maxAgeInMicros);
    });

    ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Auth
