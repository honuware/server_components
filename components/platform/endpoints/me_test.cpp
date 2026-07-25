#include "me.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/auth/session.h"
#include "db_schema/sessions.h"
#include "sql_util/table_helpers/sessions.h"
#include "sql_util/table_helpers/people.h"
#include "db_schema/people.h"
#include "util/secrets/secret_keys.h"
#include "test/src/util/database_test_helper.h"
#include "business_logic/auth/cookie_manager_test_util.h"
#include "business_logic/auth/cookie_manager_test_util.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/error_codes.h"
#include "util/json_value.h"

namespace Endpoints {
namespace {

TEST(MeTest, MeBasic) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("MeBasic", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        // Required secret for session creation
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(10LL * 60LL * 1000000LL)); // 10 minutes

        // Create validated user and session
        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "me_basic@example.com", "First", "Last" };
        personHelper.CreateFullyValidatedUser(transaction, info, "Password!");

        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, info.email, sessionToken));
        ASSERT_FALSE(sessionToken.empty());

        // Seed cookie with session token
        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        // Call endpoint
        crow::request req;
        req.method = crow::HTTPMethod::Get;
        req.url = "/api/me";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);

        // Validate the session cookie remains set
        auto cookies = cookieMgr->GetCookies();
        ASSERT_TRUE(cookies.find("session_token") != cookies.end());

        // Look up actual person_id
        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        std::string personId = person.at(std::string(DbSchema::kPeopleId));

        // Validate DB session row exists
        TableHelpers::Sessions sessionsHelper(testDb.GetDatabaseHelper());
        KeyValueTable sessionRow = sessionsHelper.LookupSessionByUuid(transaction, cookies["session_token"]);
        EXPECT_EQ(sessionRow.at(std::string(DbSchema::kSessionsPersonId)), personId);
    });

    Auth::ServerConfig::Shutdown();
}

TEST(MeTest, MeInvalid) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("MeInvalid", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(10LL * 60LL * 1000000LL));

        // Seed invalid session token cookie
        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", "invalid-session-token", cp);

        crow::request req;
        req.method = crow::HTTPMethod::Get;
        req.url = "/api/me";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 401);

        // Verify JSON error response format
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kNotAuthenticated);
        EXPECT_EQ(body["status"].Get<int64_t>(), 401);
    });

    Auth::ServerConfig::Shutdown();
}

} // namespace
} // namespace Endpoints