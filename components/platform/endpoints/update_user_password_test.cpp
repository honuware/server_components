#include "update_user_password.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/auth/session.h"
#include "db_schema/auth_events.h"
#include "db_schema/people.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/table_helpers/auth_events.h"
#include "sql_util/table_helpers/people.h"
#include "util/secrets/secret_keys.h"
#include "util/types.h"
#include "test/src/util/database_test_helper.h"
#include "business_logic/auth/cookie_manager_test_util.h"
#include "util/error_codes.h"
#include "util/json_value.h"
#include "util/secrets/secrets_helper_test_util.h"

namespace Endpoints {
namespace {

TEST(UpdateUserPasswordTest, UpdateUserPasswordBasic) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateUserPasswordBasic", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL)); // 15 minutes

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());

        // Create user with known password
        Auth::PersonInfo info{ "pw_user@example.com", "First", "Last" };
        personHelper.CreateFullyValidatedUser(transaction, info, "OldPassword!");

        // Create a session token and seed cookie
        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, info.email, sessionToken));
        ASSERT_FALSE(sessionToken.empty());

        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        // Call endpoint: change password
        Json::JsonObject body;
        body["old_password"] = "OldPassword!";
        body["new_password"] = "NewPassword!";

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/update_user_password";
        req.body = Json::Value(std::move(body)).ToString();

        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);

        // Verify DB state: old password fails; new password succeeds.
        EXPECT_FALSE(personHelper.VerifyPassword(transaction, info.email, "OldPassword!"));
        EXPECT_TRUE(personHelper.VerifyPassword(transaction, info.email, "NewPassword!"));
    });

    Auth::ServerConfig::Shutdown();
}

TEST(UpdateUserPasswordTest, UpdateUserPasswordNoUser) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateUserPasswordNoUser", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        // No session cookie

        Json::JsonObject body;
        body["old_password"] = "OldPassword!";
        body["new_password"] = "NewPassword!";

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/update_user_password";
        req.body = Json::Value(std::move(body)).ToString();

        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 401);

        // Verify JSON error response format
        Json::Value respBody = Json::Value::FromText(resp.body);
        EXPECT_EQ(respBody["type"].Get<std::string>(), ErrorCodes::kNotAuthenticated);
        EXPECT_EQ(respBody["status"].Get<int64_t>(), 401);
    });

    Auth::ServerConfig::Shutdown();
}

TEST(UpdateUserPasswordTest, ClearsMustChangePasswordFlag) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ClearsMustChangePasswordFlag", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL));

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "clearflag@example.com", "Clear", "Flag" };
        personHelper.CreateFullyValidatedUser(transaction, info, "OldPassword1!");

        // Set must_change_password = true
        TableHelpers::People peopleTH(testDb.GetDatabaseHelper());
        KeyValueTable person = peopleTH.LookupPersonByEmail(transaction, info.email);
        int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));
        KeyValueTable updates = {
            { std::string(DbSchema::kPeopleMustChangePassword), "true" }
        };
        DbCrud::UpdateRow(
            transaction, testDb.GetDatabaseHelper(),
            DbSchema::kPeopleTable, DbSchema::kPeopleId,
            StringFromInt(personId), updates);

        // Verify the flag is set
        KeyValueTable beforeRow = peopleTH.GetPersonById(transaction, personId);
        EXPECT_EQ(beforeRow[std::string(DbSchema::kPeopleMustChangePassword)], "t");

        // Create session
        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, info.email, sessionToken));
        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        // Change password
        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/update_user_password";
        req.body = R"({"old_password": "OldPassword1!", "new_password": "NewPassword2!"})";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);

        // Verify the flag is now cleared
        KeyValueTable afterRow = peopleTH.GetPersonById(transaction, personId);
        EXPECT_EQ(afterRow[std::string(DbSchema::kPeopleMustChangePassword)], "f");
    });

    Auth::ServerConfig::Shutdown();
}

// Phase 9.1 of the security review: a successful password change
// records a `password_changed` row in auth_events tied to the
// logged-in person_id. Forensic queries trigger on a flurry of
// these because that's the signature of a takeover spree.
TEST(UpdateUserPasswordTest, RecordsAuthEventOnSuccess) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateUserPasswordRecordsAuthEventOnSuccess",
        [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction,
            Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL));

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "pw_audit@example.com", "F", "L" };
        personHelper.CreateFullyValidatedUser(
            transaction, info, "OldPassword!");

        TableHelpers::People people(testDb.GetDatabaseHelper());
        std::string personId = people.LookupPersonByEmail(transaction,
            info.email).at(std::string(DbSchema::kPeopleId));

        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(
            transaction, secrets, info.email, sessionToken));
        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/update_user_password";
        req.body = R"({"old_password":"OldPassword!","new_password":"NewPassword!"})";
        req.add_header("User-Agent", "UnitTestUA/9.1");
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        ASSERT_EQ(resp.code, 200);

        KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
            "SELECT kind, person_id, user_agent FROM auth_events "
            "WHERE kind = $1 ORDER BY id DESC LIMIT 1",
            std::string(DbSchema::kAuthEventsKindPasswordChanged));
        EXPECT_EQ(row.at("kind"),
            std::string(DbSchema::kAuthEventsKindPasswordChanged));
        EXPECT_EQ(row.at("person_id"), personId);
        EXPECT_EQ(row.at("user_agent"), "UnitTestUA/9.1");
    });
    Auth::ServerConfig::Shutdown();
}

// A wrong old_password attempt MUST NOT record password_changed —
// the change didn't happen. Failed password-change attempts are
// not currently part of the audit log (would be noise from
// fat-fingers); only successful changes are.
TEST(UpdateUserPasswordTest, WrongOldPasswordDoesNotRecordAuthEvent) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "UpdateUserPasswordWrongOldPasswordDoesNotRecord",
        [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction,
            Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL));

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "pw_audit_wrong@example.com", "F", "L" };
        personHelper.CreateFullyValidatedUser(
            transaction, info, "OldPassword!");

        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(
            transaction, secrets, info.email, sessionToken));
        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/update_user_password";
        req.body = R"({"old_password":"WrongPassword!","new_password":"NewPassword!"})";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 401);

        std::string count = transaction.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM auth_events WHERE kind = $1",
            std::string(DbSchema::kAuthEventsKindPasswordChanged));
        EXPECT_EQ(count, "0");
    });
    Auth::ServerConfig::Shutdown();
}

} // namespace
} // namespace Endpoints