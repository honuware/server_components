#include "login.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/auth/session.h"
#include "db_schema/auth_events.h"
#include "db_schema/device_tokens.h"
#include "db_schema/login_attempts.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "sql_util/table_helpers/auth_events.h"
#include "sql_util/table_helpers/login_attempts.h"
#include "sql_util/table_helpers/people.h"
#include "db_schema/people.h"
#include "sql_util/table_helpers/sessions.h"
#include "db_schema/sessions.h"
#include "test/src/util/database_test_helper.h"
#include "business_logic/auth/cookie_manager_test_util.h"
#include "util/error_codes.h"
#include "util/json_value.h"
#include "util/thread_pool.h"

namespace Endpoints {
namespace {

TEST(LoginTest, LoginBasic) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginBasic", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        // Required secrets for session handling
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL)); // 15 minutes
        // Add device token secret (unused because remember = false)
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL)); // 7 days
        // login checks kWebsiteAddress is set on the success path (redirect
        // target) and 401s if empty. In the app suite this arrives via the app's
        // registered secret defaults; honuware's framework-only runner has none,
        // so the framework test must seed it explicitly.
        secrets->AddSecret(transaction, Secrets::kWebsiteAddress, "example.test");

        // Create a fully validated user
        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "login_user@example.com", "First", "Last" };
        personHelper.CreateFullyValidatedUser(transaction, info, "MyPassword!");

		Json::JsonObject body;
		body["email"] = info.email;
		body["password"] = "MyPassword!";
		body["remember"] = false;

		std::string bodyStr = Json::Value(std::move(body)).ToString();
        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/login";
        req.body = bodyStr;
        crow::response resp;

        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        // Phase 5.4 of the security review: /api/login enqueues an
        // async login_attempts INSERT on the ThreadPool. The test's
        // outer transaction shares the libpqxx connection, so any
        // SQL on the test thread BEFORE the async write completes
        // throws "command still active". Flush here, before the
        // first DB-touching assertion runs.
        ThreadPool::GetInstance().Shutdown();

        EXPECT_TRUE(resp.code == 200);

        auto cookieManager = endpointHelper.GetCookieManagerTest();

        // session_token + csrft (Phase 4.1 of the security review:
        // every successful login also mints a companion CSRF cookie).
        ASSERT_EQ(cookieManager->GetCookies().size(), 2);
        ASSERT_TRUE(cookieManager->GetCookies().find("session_token") != cookieManager->GetCookies().end());
        ASSERT_TRUE(cookieManager->GetCookies().find("csrft") != cookieManager->GetCookies().end());
        // Ensure device token NOT set
        ASSERT_TRUE(cookieManager->GetCookies().find("device_token") == cookieManager->GetCookies().end());

        // Look up actual person_id
        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        std::string personId = person.at(std::string(DbSchema::kPeopleId));

        // Verify session token is valid in DB
        TableHelpers::Sessions sessions(testDb.GetDatabaseHelper());
        KeyValueTable row = sessions.LookupSessionByUuid(transaction, cookieManager->GetCookies().at("session_token"));
        EXPECT_EQ(row.at(std::string(DbSchema::kSessionsPersonId)), personId);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(LoginTest, LoginRemember) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginRemember", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL)); // 15 minutes
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL)); // 7 days
        // login 401s on the success path if kWebsiteAddress is empty; honuware's
        // framework-only runner loads no app secret defaults, so seed it here.
        secrets->AddSecret(transaction, Secrets::kWebsiteAddress, "example.test");

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "login_remember_user@example.com", "First", "Last" };
        personHelper.CreateFullyValidatedUser(transaction, info, "MyPassword!");

		Json::JsonObject body;
		body["email"] = info.email;
		body["password"] = "MyPassword!";
		body["remember"] = true;

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/login";
		req.body = Json::Value(std::move(body)).ToString();
        crow::response resp;

        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        // Phase 5.4: flush async login_attempts writer BEFORE any
        // DB assertions on the test thread. Otherwise the test's
        // SELECT collides with the worker's INSERT on the same
        // libpqxx connection ("command still active").
        ThreadPool::GetInstance().Shutdown();

        // Expect redirect
        EXPECT_TRUE(resp.code == 200);

        auto cookieManager = endpointHelper.GetCookieManagerTest();
        auto cookies = cookieManager->GetCookies();
        // Expect both session and device token cookies
        ASSERT_TRUE(cookies.find("session_token") != cookies.end());
        ASSERT_TRUE(cookies.find("device_token") != cookies.end());

        // Look up actual person_id
        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        std::string personId = person.at(std::string(DbSchema::kPeopleId));

        // Validate session token
        TableHelpers::Sessions sessions(testDb.GetDatabaseHelper());
        KeyValueTable sessionRow = sessions.LookupSessionByUuid(transaction, cookies["session_token"]);
        EXPECT_EQ(sessionRow.at(std::string(DbSchema::kSessionsPersonId)), personId);

        // Validate device token format uuid.secret and DB entry
        const std::string& combined = cookies["device_token"];
        size_t dotPos = combined.find('.');
        ASSERT_NE(dotPos, std::string::npos);
        std::string uuid = combined.substr(0, dotPos);

        KeyValueTable deviceRow = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM device_tokens WHERE uuid = $1", uuid);
        ASSERT_FALSE(deviceRow.empty());
        EXPECT_EQ(deviceRow.at(std::string(DbSchema::kDeviceTokensPersonId)), personId);
        EXPECT_TRUE(deviceRow.at(std::string(DbSchema::kDeviceTokensRevoked)) == "f" ||
                    deviceRow.at(std::string(DbSchema::kDeviceTokensRevoked)) == "false" ||
                    deviceRow.at(std::string(DbSchema::kDeviceTokensRevoked)) == "0");
    });

    Auth::ServerConfig::Shutdown();
}

TEST(LoginTest, LoginNotFound) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginNotFound", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(10LL * 60LL * 1000000LL));

        // No user created

		Json::JsonObject body;
		body["email"] = "missing_user@example.com";
		body["password"] = "Whatever!";
		body["remember"] = false;

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/login";
		req.body = Json::Value(std::move(body)).ToString();
        crow::response resp;

        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 401);

        // Verify JSON error response format
        Json::Value bodyTest = Json::Value::FromText(resp.body);
        EXPECT_EQ(bodyTest["type"].Get<std::string>(), ErrorCodes::kInvalidCredentials);
        EXPECT_EQ(bodyTest["status"].Get<int64_t>(), 401);

        // Flush async login_attempts writer (Phase 5.4).
        ThreadPool::GetInstance().Shutdown();
    });
    Auth::ServerConfig::Shutdown();
}

// =====================================================================
// Phase 5 of the security review: rate limiting + brute-force defense.
// =====================================================================

namespace {

void SeedAuthDurationSecrets(
    Transaction& tx,
    Secrets::Test::SecretsHelperTestPtr secrets) {
    secrets->AddSecret(tx, Secrets::kAuthSessionMaxDuractioninMicros,
        std::to_string(15LL * 60LL * 1000000LL));
    secrets->AddSecret(tx, Secrets::kAuthDeviceTokenMaxDurationInMicros,
        std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL));
    secrets->AddSecret(tx, Secrets::kWebsiteAddress, "example.test");
}

std::string PostLoginBody(
    std::string_view email, std::string_view password, bool remember) {
    Json::JsonObject body;
    body["email"] = std::string(email);
    body["password"] = std::string(password);
    body["remember"] = remember;
    return Json::Value(std::move(body)).ToString();
}

}  // namespace

// Phase 5.4 of the security review: a successful login enqueues a
// row in login_attempts with success=true. Tests run async work via
// ThreadPool::Shutdown to flush and assert deterministically (same
// pattern as PersonHelperTest::SessionUsedBasic).
TEST(LoginTest, LoginRecordsSuccessfulAttemptAsync) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginRecordsSuccessfulAttemptAsync",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        SeedAuthDurationSecrets(tx, secrets);

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "rec-success@example.com", "F", "L" };
        personHelper.CreateFullyValidatedUser(tx, info, "Right!");

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/login";
        req.body = PostLoginBody(info.email, "Right!", false);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);

        // Flush any queued async work — the ThreadPool wraps the
        // test's transaction, so the row is visible inside the
        // same tx after Shutdown returns.
        ThreadPool::GetInstance().Shutdown();

        std::string countStr = tx.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM login_attempts "
            "WHERE email_lower = $1 AND success = true",
            std::string(info.email));
        EXPECT_EQ(countStr, "1");
    });
    Auth::ServerConfig::Shutdown();
}

// A wrong password records a failure row asynchronously. The gate
// reads this on the next attempt, so its accuracy depends on the
// recorder running.
TEST(LoginTest, LoginRecordsFailedAttemptAsync) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginRecordsFailedAttemptAsync",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        SeedAuthDurationSecrets(tx, secrets);

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "rec-fail@example.com", "F", "L" };
        personHelper.CreateFullyValidatedUser(tx, info, "Right!");

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/login";
        req.body = PostLoginBody(info.email, "Wrong!", false);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 401);

        ThreadPool::GetInstance().Shutdown();

        std::string countStr = tx.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM login_attempts "
            "WHERE email_lower = $1 AND success = false",
            std::string(info.email));
        EXPECT_EQ(countStr, "1");
    });
    Auth::ServerConfig::Shutdown();
}

// Phase 5.7: rate-limit refusals come back as 429 with the
// `too_many_attempts` type — distinct from a wrong password (401
// invalid_credentials) so the SPA can show the right toast, but
// still indistinguishable to an attacker (no detail about WHICH
// bucket tripped).
TEST(LoginTest, LoginRateLimitedReturns429) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginRateLimitedReturns429", [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        SeedAuthDurationSecrets(tx, secrets);
        // Tight per-email threshold so we don't have to insert 10 rows.
        secrets->AddSecret(tx,
            Secrets::kAuthLoginMaxFailuresPerEmailPerWindow, "3");
        secrets->AddSecret(tx,
            Secrets::kAuthLoginFailureWindowInMicros,
            std::to_string(60LL * 1000000LL));

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "ratelimit@example.com", "F", "L" };
        personHelper.CreateFullyValidatedUser(tx, info, "Right!");

        // Pre-seed the gate's view: 3 prior failures from this email.
        TableHelpers::LoginAttempts attempts(testDb.GetDatabaseHelper());
        for (int i = 0; i < 3; ++i) {
            attempts.RecordAttempt(tx, info.email, "10.0.0.1",
                DbSchema::kLoginAttemptsKindLogin, /*success=*/false);
        }

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/login";
        req.body = PostLoginBody(info.email, "Right!", false);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 429);
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["type"].Get<std::string>(),
                  ErrorCodes::kTooManyAttempts);
        EXPECT_EQ(body["status"].Get<int64_t>(), 429);

        // Gate refused BEFORE Argon2 ran — no session cookie set.
        auto cookies = endpointHelper.GetCookieManagerTest()->GetCookies();
        EXPECT_TRUE(cookies.find("session_token") == cookies.end());

        // The refused attempt itself does NOT add to login_attempts.
        // The 3 pre-seeded rows are why we got 429; bumping a 4th
        // would re-extend the lock window for free, which the
        // design specifically avoids (no double-counting).
        ThreadPool::GetInstance().Shutdown();
        std::string countStr = tx.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM login_attempts "
            "WHERE email_lower = $1",
            std::string(info.email));
        EXPECT_EQ(countStr, "3");
    });
    Auth::ServerConfig::Shutdown();
}

// Phase 5.3 / 5.7 interaction: an account locked at the row level
// (via `people.locked_until`) returns 429 ahead of Argon2. The
// breakdown vs RateLimitedEmail is invisible to the caller.
TEST(LoginTest, LoginAccountLockedReturns429) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginAccountLockedReturns429", [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        SeedAuthDurationSecrets(tx, secrets);

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "locked@example.com", "F", "L" };
        personHelper.CreateFullyValidatedUser(tx, info, "Right!");

        // Lock 1 hour into the future — well past any test timing.
        std::string nowUs = tx.RunSqlStatementReturningOneValue(
            "SELECT now_us()");
        int64_t lockUntil = std::stoll(nowUs) + 3600LL * 1000000LL;
        tx.RunSqlStatement(
            "UPDATE people SET locked_until = $1 WHERE email = $2",
            std::to_string(lockUntil),
            std::string(info.email));

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/login";
        // CORRECT password. The lock should still refuse it.
        req.body = PostLoginBody(info.email, "Right!", false);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 429);
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["type"].Get<std::string>(),
                  ErrorCodes::kTooManyAttempts);

        // No session cookie was set.
        auto cookies = endpointHelper.GetCookieManagerTest()->GetCookies();
        EXPECT_TRUE(cookies.find("session_token") == cookies.end());

        ThreadPool::GetInstance().Shutdown();
    });
    Auth::ServerConfig::Shutdown();
}

// The 401 response body for "wrong password" is identical to the
// 401 response body for "unknown email" — no enumeration via
// timing or response shape. The wall-clock timing-padding (Phase
// 5.7) is a separate concern; here we verify the response shape
// matches in both cases.
TEST(LoginTest, LoginWrongPasswordSameShapeAsUnknownEmail) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginWrongPasswordSameShapeAsUnknownEmail",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        SeedAuthDurationSecrets(tx, secrets);

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "real@example.com", "F", "L" };
        personHelper.CreateFullyValidatedUser(tx, info, "Right!");

        // Wrong password — known email.
        crow::request reqWrong;
        reqWrong.method = crow::HTTPMethod::POST;
        reqWrong.url = "/api/login";
        reqWrong.body = PostLoginBody(info.email, "Wrong!", false);
        crow::response respWrong;
        endpointHelper.GetWebApp().GetApp().handle_full(reqWrong, respWrong);

        // Phase 5.4: flush the first request's async login_attempts
        // writer before issuing the second request, so the second
        // handle_full's synchronous SELECT doesn't collide with the
        // first one's INSERT on the shared libpqxx connection.
        ThreadPool::GetInstance().Shutdown();

        // Unknown email — wrong password (well, any password).
        crow::request reqUnknown;
        reqUnknown.method = crow::HTTPMethod::POST;
        reqUnknown.url = "/api/login";
        reqUnknown.body = PostLoginBody(
            "ghost@example.com", "Anything!", false);
        crow::response respUnknown;
        endpointHelper.GetWebApp().GetApp().handle_full(reqUnknown, respUnknown);

        EXPECT_EQ(respWrong.code, 401);
        EXPECT_EQ(respUnknown.code, 401);

        Json::Value bodyWrong = Json::Value::FromText(respWrong.body);
        Json::Value bodyUnknown = Json::Value::FromText(respUnknown.body);
        EXPECT_EQ(bodyWrong["type"].Get<std::string>(),
                  ErrorCodes::kInvalidCredentials);
        EXPECT_EQ(bodyUnknown["type"].Get<std::string>(),
                  ErrorCodes::kInvalidCredentials);
        EXPECT_EQ(bodyWrong["detail"].Get<std::string>(),
                  bodyUnknown["detail"].Get<std::string>());

        ThreadPool::GetInstance().Shutdown();
    });
    Auth::ServerConfig::Shutdown();
}

// =====================================================================
// Phase 9.1 of the security review: auth_events forensic audit log.
// /api/login writes one row per outcome (login_success on a clean
// login, login_failure on a wrong password, plus a separate
// account_locked row the moment the gate flips the lock).
// =====================================================================

// Successful login records a login_success row with the resolved
// person_id, client IP, and user-agent so a later audit can
// reconstruct exactly who got in, from where, with what client.
TEST(LoginTest, LoginSuccessRecordsAuthEvent) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginSuccessRecordsAuthEvent",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        SeedAuthDurationSecrets(tx, secrets);

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "auth-evt-success@example.com", "F", "L" };
        personHelper.CreateFullyValidatedUser(tx, info, "Right!");

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/login";
        req.body = PostLoginBody(info.email, "Right!", false);
        req.add_header("User-Agent", "UnitTestUA/9.1");
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);

        // Flush the async login_attempts writer so the test's
        // SELECTs don't collide on the shared connection (the
        // auth_events insert is synchronous, but the recorder
        // for login_attempts is not).
        ThreadPool::GetInstance().Shutdown();

        TableHelpers::People people(testDb.GetDatabaseHelper());
        std::string personId = people.LookupPersonByEmail(tx, info.email)
            .at(std::string(DbSchema::kPeopleId));

        KeyValueTable row = tx.RunSqlStatementReturningOneRow(
            "SELECT kind, person_id, user_agent FROM auth_events "
            "WHERE kind = $1 ORDER BY id DESC LIMIT 1",
            std::string(DbSchema::kAuthEventsKindLoginSuccess));
        EXPECT_EQ(row.at("kind"),
            std::string(DbSchema::kAuthEventsKindLoginSuccess));
        EXPECT_EQ(row.at("person_id"), personId);
        EXPECT_EQ(row.at("user_agent"), "UnitTestUA/9.1");
    });
    Auth::ServerConfig::Shutdown();
}

// A failed login (known email, wrong password) emits a
// login_failure row. PersonHelper deliberately does NOT echo the
// resolved person_id on failure (see person.cpp line ~382), so the
// row carries person_id=NULL — the attempted email in detail_json
// is the only forensic anchor on the failure path.
TEST(LoginTest, LoginFailureRecordsAuthEvent) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginFailureRecordsAuthEvent",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        SeedAuthDurationSecrets(tx, secrets);

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "auth-evt-fail@example.com", "F", "L" };
        personHelper.CreateFullyValidatedUser(tx, info, "Right!");

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/login";
        req.body = PostLoginBody(info.email, "Wrong!", false);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 401);
        ThreadPool::GetInstance().Shutdown();

        KeyValueTable row = tx.RunSqlStatementReturningOneRow(
            "SELECT kind, person_id, detail_json FROM auth_events "
            "WHERE kind = $1 ORDER BY id DESC LIMIT 1",
            std::string(DbSchema::kAuthEventsKindLoginFailure));
        EXPECT_EQ(row.at("kind"),
            std::string(DbSchema::kAuthEventsKindLoginFailure));
        EXPECT_NE(row.at("detail_json").find(info.email), std::string::npos);

        std::string nullCount = tx.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM auth_events "
            "WHERE kind = $1 AND person_id IS NULL",
            std::string(DbSchema::kAuthEventsKindLoginFailure));
        EXPECT_EQ(nullCount, "1");
    });
    Auth::ServerConfig::Shutdown();
}

// Failed login for an unknown email still emits a login_failure
// row, but person_id is NULL (we couldn't identify a user). The
// detail_json carries the attempted email — that's the only
// signal a forensic query has for "what was being brute-forced".
TEST(LoginTest, LoginFailureUnknownEmailRecordsNullPersonId) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginFailureUnknownEmailRecordsNullPersonId",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        SeedAuthDurationSecrets(tx, secrets);

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/login";
        req.body = PostLoginBody("ghost@example.com", "Anything!", false);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 401);
        ThreadPool::GetInstance().Shutdown();

        std::string count = tx.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM auth_events "
            "WHERE kind = $1 AND person_id IS NULL",
            std::string(DbSchema::kAuthEventsKindLoginFailure));
        EXPECT_EQ(count, "1");
    });
    Auth::ServerConfig::Shutdown();
}

// When a failed login crosses the lockout threshold and flips
// `people.locked_until` for the first time, the endpoint emits a
// SECOND auth_events row of kind `account_locked` alongside the
// usual login_failure. Two rows is the signal an alerting query
// uses to distinguish "another wrong password" from "an account
// just got locked".
TEST(LoginTest, LoginAccountLockedEmitsAccountLockedEvent) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginAccountLockedEmitsAccountLockedEvent",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        SeedAuthDurationSecrets(tx, secrets);
        // Lock after a SINGLE failure so we only need one request to
        // observe the just-locked event.
        secrets->AddSecret(tx,
            Secrets::kAuthAccountLockoutAfterFailures, "1");
        secrets->AddSecret(tx,
            Secrets::kAuthAccountLockoutDurationInMicros,
            std::to_string(60LL * 1000000LL));

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "auth-evt-locked@example.com", "F", "L" };
        personHelper.CreateFullyValidatedUser(tx, info, "Right!");

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/login";
        req.body = PostLoginBody(info.email, "Wrong!", false);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 401);
        ThreadPool::GetInstance().Shutdown();

        // Both rows present: the failure AND the just-locked sentinel.
        std::string failCount = tx.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM auth_events WHERE kind = $1",
            std::string(DbSchema::kAuthEventsKindLoginFailure));
        std::string lockCount = tx.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM auth_events WHERE kind = $1",
            std::string(DbSchema::kAuthEventsKindAccountLocked));
        EXPECT_EQ(failCount, "1");
        EXPECT_EQ(lockCount, "1");
    });
    Auth::ServerConfig::Shutdown();
}

} // namespace
} // namespace Endpoints