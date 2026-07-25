#include "remember.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/auth/session.h"
#include "db_schema/device_tokens.h"
#include "db_schema/login_attempts.h"
#include "db_schema/sessions.h"
#include "sql_util/table_helpers/login_attempts.h"
#include "sql_util/table_helpers/sessions.h"
#include "sql_util/table_helpers/people.h"
#include "db_schema/people.h"
#include "business_logic/auth/cookie_manager.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/secrets/secret_keys.h"
#include "util/thread_pool.h"
#include "business_logic/auth/cookie_manager_test_util.h"
#include "test/src/util/database_test_helper.h"
#include "util/error_codes.h"
#include "util/json_value.h"

namespace Endpoints {
namespace {

TEST(RememberTest, RememberBasic) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RememberBasic", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL)); // 15 minutes
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL)); // 7 days

        // Create validated user
        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "remember_basic@example.com", "First", "Last" };
        personHelper.CreateFullyValidatedUser(transaction, info, "Password!");

        // Create initial device token (uuid + secret)
        std::string deviceUuid;
        std::string deviceSecretBase64;
        ASSERT_TRUE(personHelper.CreateDeviceToken(
            transaction, secrets, info.email, deviceUuid, deviceSecretBase64));
        std::string combined = deviceUuid + "." + deviceSecretBase64;

        // Optionally create a previous session token (to ensure new one is set)
        std::string priorSessionUuid;
        ASSERT_TRUE(personHelper.CreateSessionToken(
            transaction, secrets, info.email, priorSessionUuid));

        // Seed cookie with device token
        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("device_token", combined, cp);
        // Also seed the old session cookie (will be replaced)
        cookieMgr->SetCookie("session_token", priorSessionUuid, cp);

        // Call endpoint
        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/remember";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        // Phase 5.4: flush async login_attempts writer BEFORE any
        // DB assertions on the test thread. The worker shares the
        // libpqxx connection; otherwise the test's SELECT collides
        // with the worker's INSERT ("command still active").
        ThreadPool::GetInstance().Shutdown();

        EXPECT_EQ(resp.code, 200);

        auto cookies = cookieMgr->GetCookies();
        // New session token must exist and differ from prior
        ASSERT_TRUE(cookies.find("session_token") != cookies.end());
        EXPECT_NE(cookies["session_token"], priorSessionUuid);

        // Device token present and rotated (same uuid, different secret)
        ASSERT_TRUE(cookies.find("device_token") != cookies.end());
        std::string newCombined = cookies["device_token"];
        auto [newUuid, newSecret] = SplitString(newCombined, ".");

        EXPECT_NE(newUuid, deviceUuid);
        EXPECT_NE(newSecret, deviceSecretBase64);

        // Look up actual person_id
        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        std::string personId = person.at(std::string(DbSchema::kPeopleId));

        // Validate new session in DB
        TableHelpers::Sessions sessionsHelper(testDb.GetDatabaseHelper());
        KeyValueTable sessionRow = sessionsHelper.LookupSessionByUuid(transaction, cookies["session_token"]);
        EXPECT_EQ(sessionRow.at(std::string(DbSchema::kSessionsPersonId)), personId);

        // Validate device token still valid and not revoked
        KeyValueTable deviceRow = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM device_tokens WHERE uuid = $1", newUuid);
        ASSERT_FALSE(deviceRow.empty());
        EXPECT_EQ(deviceRow.at(std::string(DbSchema::kDeviceTokensPersonId)), personId);
        EXPECT_TRUE(deviceRow.at(std::string(DbSchema::kDeviceTokensRevoked)) == "f" ||
                    deviceRow.at(std::string(DbSchema::kDeviceTokensRevoked)) == "false" ||
                    deviceRow.at(std::string(DbSchema::kDeviceTokensRevoked)) == "0");
    });

    Auth::ServerConfig::Shutdown();
}

TEST(RememberTest, RememberInvalid) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RememberInvalid", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL));
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL));

        // Seed invalid device token cookie
        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("device_token",
            "00000000-0000-0000-0000-000000000000.invalidsecret", cp);

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/remember";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        // Phase 5.4: flush async login_attempts writer (uniform
        // pattern — flush before any further work).
        ThreadPool::GetInstance().Shutdown();

        EXPECT_EQ(resp.code, 401);

        // Verify JSON error response format
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kNotAuthenticated);
        EXPECT_EQ(body["status"].Get<int64_t>(), 401);

        // No session cookie set
        auto cookies = cookieMgr->GetCookies();
        EXPECT_TRUE(cookies.find("session_token") == cookies.end());
    });

    Auth::ServerConfig::Shutdown();
}

// Phase 5.6 of the security review: per-IP rate-limit gate on
// /api/remember. There's no authenticated email at this point, so
// per-IP is the only useful bucket.
TEST(RememberTest, RememberRateLimitedPerIp) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RememberRateLimitedPerIp",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            auto secrets = endpointHelper.GetSecretsHelper();
            secrets->AddSecret(transaction,
                Secrets::kAuthRememberMaxFailuresPerIpPerWindow, "3");
            secrets->AddSecret(transaction,
                Secrets::kAuthRememberFailureWindowInMicros,
                std::to_string(60LL * 1000000LL));

            // Pre-seed the gate's view: 3 prior remember failures
            // from this IP.
            TableHelpers::LoginAttempts attempts(testDb.GetDatabaseHelper());
            for (int i = 0; i < 3; ++i) {
                attempts.RecordAttempt(transaction,
                    /*email=*/"",
                    "10.0.0.1",
                    DbSchema::kLoginAttemptsKindRemember,
                    /*success=*/false);
            }

            // No device_token cookie set — would normally yield 401
            // NotAuthenticated. Gate refuses with 429 first.
            crow::request req;
            req.method = crow::HTTPMethod::POST;
            req.url = "/api/remember";
            req.remote_ip_address = "10.0.0.1";
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

            EXPECT_EQ(resp.code, 429);
            Json::Value body = Json::Value::FromText(resp.body);
            EXPECT_EQ(body["type"].Get<std::string>(),
                      ErrorCodes::kTooManyAttempts);

            ThreadPool::GetInstance().Shutdown();
        });
    Auth::ServerConfig::Shutdown();
}

// A failed remember attempt records async with kind=remember.
TEST(RememberTest, RememberRecordsFailureAttemptAsync) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RememberRecordsFailureAttemptAsync",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);

            crow::request req;
            req.method = crow::HTTPMethod::POST;
            req.url = "/api/remember";
            req.remote_ip_address = "10.0.0.99";
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

            EXPECT_EQ(resp.code, 401);

            ThreadPool::GetInstance().Shutdown();

            std::string countStr = transaction.RunSqlStatementReturningOneValue(
                "SELECT count(*) FROM login_attempts "
                "WHERE kind = $1 AND success = false AND ip = $2",
                std::string(DbSchema::kLoginAttemptsKindRemember),
                std::string("10.0.0.99"));
            EXPECT_EQ(countStr, "1");
        });
    Auth::ServerConfig::Shutdown();
}

} // namespace
} // namespace Endpoints