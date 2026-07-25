#include "logout.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/auth/session.h"
#include "db_schema/auth_events.h"
#include "db_schema/people.h"
#include "db_schema/device_tokens.h"
#include "db_schema/sessions.h"
#include "sql_util/table_helpers/auth_events.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/table_helpers/sessions.h"
#include "util/secrets/secret_keys.h"
#include "test/src/util/database_test_helper.h"
#include "business_logic/auth/cookie_manager_test_util.h"
#include "util/secrets/secrets_helper_test_util.h"

namespace Endpoints {
namespace {

TEST(LogoutTest, LogoutBasic) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LogoutBasic", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL));
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL));

        // Create validated user
        Auth::PersonHelper helper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "logout_user@example.com", "First", "Last" };
        helper.CreateFullyValidatedUser(transaction, info, "Pwd!");

        // Lookup person id
        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());
        int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

        // Create session and device tokens
        std::string sessionToken;
        ASSERT_TRUE(helper.CreateSessionToken(transaction, secrets, info.email, sessionToken));
        std::string deviceUuid, deviceSecretB64;
        ASSERT_TRUE(helper.CreateDeviceToken(transaction, secrets, info.email, deviceUuid, deviceSecretB64));

        // Seed cookies
        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);
        cookieMgr->SetCookie("device_token", deviceUuid + std::string(".") + deviceSecretB64, cp);

        // Sanity pre-check: entries exist
        KeyValueTable srow = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM sessions WHERE uuid = $1", sessionToken);
        ASSERT_FALSE(srow.empty());
        KeyValueTable drow = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM device_tokens WHERE uuid = $1", deviceUuid);
        ASSERT_FALSE(drow.empty());

        // Call endpoint
        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/logout";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        // Always succeed
        EXPECT_EQ(resp.code, 200);

        // Validate database cleanup
        int sessionCount = std::stoi(transaction.RunSqlStatementReturningOneValue(
            "SELECT COUNT(*) FROM sessions WHERE person_id = $1", std::to_string(personId)));
        int deviceCount = std::stoi(transaction.RunSqlStatementReturningOneValue(
            "SELECT COUNT(*) FROM device_tokens WHERE person_id = $1", std::to_string(personId)));
        EXPECT_EQ(sessionCount, 0);
        EXPECT_EQ(deviceCount, 0);

        // Validate cookies cleared (empty value)
        auto cookies = cookieMgr->GetCookies();
        auto itSess = cookies.find("session_token");
        auto itDev = cookies.find("device_token");
        ASSERT_NE(itSess, cookies.end());
        ASSERT_NE(itDev, cookies.end());
        EXPECT_TRUE(itSess->second.empty());
        EXPECT_TRUE(itDev->second.empty());
    });

    Auth::ServerConfig::Shutdown();
}

// Phase 4.4 of the security review: logout MUST also clear the
// csrft companion cookie. Otherwise the SPA keeps a stale token that
// no longer corresponds to any session, and the next login (which
// mints a new csrft) leaves the old cookie hanging around for the
// browser to send instead of the fresh one.
TEST(LogoutTest, LogoutClearsCsrfCookie) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LogoutClearsCsrfCookie", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL));
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL));

        Auth::PersonHelper helper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "logout_csrf@example.com", "First", "Last" };
        helper.CreateFullyValidatedUser(transaction, info, "Pwd!");

        std::string sessionToken;
        ASSERT_TRUE(helper.CreateSessionToken(
            transaction, secrets, info.email, sessionToken));

        // Seed all three auth-related cookies a real browser would
        // hold after a successful login.
        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);
        Auth::CookieProperties csrfCp = cp;
        csrfCp.httpOnly = false;
        cookieMgr->SetCookie("csrft", "live-csrf-token", csrfCp);

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/logout";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
        EXPECT_EQ(resp.code, 200);

        // The csrft cookie should now be empty.
        auto cookies = cookieMgr->GetCookies();
        auto itCsrf = cookies.find("csrft");
        ASSERT_NE(itCsrf, cookies.end())
            << "logout must touch the csrft cookie (set it to empty)";
        EXPECT_TRUE(itCsrf->second.empty())
            << "csrft cookie value must be cleared on logout";

        // The clear properties must mirror how the cookie was set:
        // HttpOnly=false (so the browser parses the same Set-Cookie
        // shape and replaces the existing cookie cleanly), Path=/.
        auto props = cookieMgr->GetCookieProperties();
        auto itProps = props.find("csrft");
        ASSERT_NE(itProps, props.end());
        EXPECT_FALSE(itProps->second.httpOnly)
            << "clear must use HttpOnly=false to match the set attributes";
        EXPECT_EQ(itProps->second.path, "/");
        EXPECT_EQ(itProps->second.maxAgeInMicros, 0)
            << "Max-Age=0 expires the cookie immediately";
    });

    Auth::ServerConfig::Shutdown();
}

// Phase 6.1 of the security review: every cleared cookie ships with
// SameSite=Lax — the same value session.cpp uses at set-time.
// Browsers refuse to apply a deletion when Set-Cookie attributes
// disagree with the live cookie; the previous implementation used
// SameSite=None at clear-time and got bitten by this. Lock the
// fixed contract.
TEST(LogoutTest, LogoutClearedCookiesUseSameSiteLax) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LogoutClearedCookiesUseSameSiteLax",
        [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL));
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL));

        Auth::PersonHelper helper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{
            "logout_samesite@example.com", "First", "Last" };
        helper.CreateFullyValidatedUser(transaction, info, "Pwd!");

        std::string sessionToken;
        ASSERT_TRUE(helper.CreateSessionToken(
            transaction, secrets, info.email, sessionToken));

        // Seed an authenticated session with a None-SameSite cookie
        // (matching how cookies were sent by the browser before the
        // fix landed). Logout must overwrite this with Lax.
        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/logout";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
        ASSERT_EQ(resp.code, 200);

        auto props = cookieMgr->GetCookieProperties();
        for (const auto& name : {"session_token", "device_token", "csrft"}) {
            auto it = props.find(name);
            ASSERT_NE(it, props.end()) << "cleared cookie missing: " << name;
            EXPECT_EQ(it->second.sameSite,
                      Auth::CookieSameSitePolicy::Lax)
                << "cleared " << name << " must use SameSite=Lax to "
                   "match session.cpp's set-time attributes";
            EXPECT_EQ(it->second.maxAgeInMicros, 0)
                << "cleared " << name << " must use Max-Age=0";
            EXPECT_EQ(it->second.path, "/")
                << "cleared " << name << " must use Path=/";
        }

        // session_token / device_token stay HttpOnly; csrft does not.
        EXPECT_TRUE(props.at("session_token").httpOnly);
        EXPECT_TRUE(props.at("device_token").httpOnly);
        EXPECT_FALSE(props.at("csrft").httpOnly);
    });

    Auth::ServerConfig::Shutdown();
}

// Phase 9.1 of the security review: an authenticated /api/logout
// records a `logout` row in auth_events tied to the logged-in
// person_id. Anonymous calls — no valid session — are silently
// 200 (the design) and MUST NOT generate audit rows: otherwise an
// attacker pinging /api/logout in a loop fills the audit table.
TEST(LogoutTest, LogoutAuthenticatedRecordsAuthEvent) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LogoutAuthenticatedRecordsAuthEvent",
        [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL));
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL));

        Auth::PersonHelper helper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "logout_audit@example.com", "F", "L" };
        helper.CreateFullyValidatedUser(transaction, info, "Pwd!");

        TableHelpers::People people(testDb.GetDatabaseHelper());
        std::string personId = people.LookupPersonByEmail(transaction,
            info.email).at(std::string(DbSchema::kPeopleId));

        std::string sessionToken;
        ASSERT_TRUE(helper.CreateSessionToken(
            transaction, secrets, info.email, sessionToken));

        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/logout";
        req.add_header("User-Agent", "UnitTestUA/9.1");
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
        EXPECT_EQ(resp.code, 200);

        KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
            "SELECT kind, person_id, user_agent FROM auth_events "
            "WHERE kind = $1 ORDER BY id DESC LIMIT 1",
            std::string(DbSchema::kAuthEventsKindLogout));
        EXPECT_EQ(row.at("kind"),
            std::string(DbSchema::kAuthEventsKindLogout));
        EXPECT_EQ(row.at("person_id"), personId);
        EXPECT_EQ(row.at("user_agent"), "UnitTestUA/9.1");
    });
    Auth::ServerConfig::Shutdown();
}

// Anonymous logout is a no-op (no session cookie or an unknown
// one). It MUST NOT emit an auth_events row: the endpoint can't
// attribute it to a user, and an unauthenticated audit row is
// noise an attacker can manufacture freely.
TEST(LogoutTest, LogoutAnonymousDoesNotRecordAuthEvent) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LogoutAnonymousDoesNotRecordAuthEvent",
        [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/logout";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
        EXPECT_EQ(resp.code, 200);

        std::string count = transaction.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM auth_events WHERE kind = $1",
            std::string(DbSchema::kAuthEventsKindLogout));
        EXPECT_EQ(count, "0");
    });
    Auth::ServerConfig::Shutdown();
}

} // namespace
} // namespace Endpoints