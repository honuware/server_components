#include "set_user_info.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/auth/session.h"
#include "util/secrets/secret_keys.h"
#include "test/src/util/database_test_helper.h"
#include "business_logic/auth/cookie_manager_test_util.h"
#include "util/error_codes.h"
#include "util/json_value.h"
#include "util/secrets/secrets_helper_test_util.h"

namespace Endpoints {
namespace {

TEST(SetUserInfoTest, SetUserInfoBasicSetUserInfo) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetUserInfoBasicSetUserInfo", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL)); // 15 minutes

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());

        // Create user
        Auth::PersonInfo info{ "info_user@example.com", "First", "Last" };
        personHelper.CreateFullyValidatedUser(transaction, info, "MyPassword!");

        // Create a session token and seed cookie
        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, info.email, sessionToken));
        ASSERT_FALSE(sessionToken.empty());

        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        // Lookup person id (so we can validate via LookupPerson after update)
        Auth::PersonInfo before;
        {
            // Need personId from session creation path: easiest is to lookup by email using PersonHelper.
            // PersonHelper::LookupPerson expects id, so get id via a direct db query against people.
            KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
                "SELECT id FROM people WHERE email = $1", info.email);
            ASSERT_FALSE(row.empty());
            int64_t personId = std::stoll(row.at("id"));

            ASSERT_TRUE(personHelper.LookupPerson(transaction, personId, before));
            ASSERT_EQ(before.email, "info_user@example.com");
            ASSERT_EQ(before.firstName, "First");
            ASSERT_EQ(before.lastName, "Last");

            // Update via endpoint (all 3 fields)
            Json::JsonObject body;
            body["email"] = "new_email@example.com";
            body["first_name"] = "NewFirst";
            body["last_name"] = "NewLast";

            crow::request req;
            req.method = crow::HTTPMethod::POST;
            req.url = "/api/set_user_info";
            req.body = Json::Value(std::move(body)).ToString();

            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

            EXPECT_EQ(resp.code, 200);

            Auth::PersonInfo after;
            ASSERT_TRUE(personHelper.LookupPerson(transaction, personId, after));
            EXPECT_EQ(after.email, "new_email@example.com");
            EXPECT_EQ(after.firstName, "NewFirst");
            EXPECT_EQ(after.lastName, "NewLast");
        }
    });

    Auth::ServerConfig::Shutdown();
}

TEST(SetUserInfoTest, SetUserInfoNoUser) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateUserInfoNoUser", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        // No session cookie

        Json::JsonObject body;
        body["first_name"] = "NewFirst";

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/set_user_info";
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

// Phase 3.9 of the security review: even if the request body contains a
// person_id, the endpoint must operate on the SESSION user — never on
// some other person's row. This regression sets up two users, logs in
// as user A, and POSTs a body that includes user B's id along with
// new field values. We expect user A's record to update and user B's
// record to be untouched.
TEST(SetUserInfoTest, SetUserInfoIgnoresPersonIdInBody) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetUserInfoIgnoresPersonIdInBody",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);

            auto secrets = endpointHelper.GetSecretsHelper();
            secrets->AddSecret(transaction,
                Secrets::kAuthSessionMaxDuractioninMicros,
                std::to_string(15LL * 60LL * 1000000LL));

            Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());

            // User A — the session user.
            Auth::PersonInfo aInfo{ "victim_a@example.com", "AFirst", "ALast" };
            personHelper.CreateFullyValidatedUser(transaction, aInfo, "Password!");

            // User B — the target the attacker is trying to mutate.
            Auth::PersonInfo bInfo{ "victim_b@example.com", "BFirst", "BLast" };
            personHelper.CreateFullyValidatedUser(transaction, bInfo, "Password!");

            int64_t aId = std::stoll(
                transaction.RunSqlStatementReturningOneRow(
                    "SELECT id FROM people WHERE email = $1", aInfo.email)
                    .at("id"));
            int64_t bId = std::stoll(
                transaction.RunSqlStatementReturningOneRow(
                    "SELECT id FROM people WHERE email = $1", bInfo.email)
                    .at("id"));

            // Log in as user A.
            std::string sessionToken;
            ASSERT_TRUE(personHelper.CreateSessionToken(
                transaction, secrets, aInfo.email, sessionToken));
            auto cookieMgr = endpointHelper.GetCookieManagerTest();
            Auth::CookieProperties cp;
            cp.path = "/";
            cp.sameSite = Auth::CookieSameSitePolicy::None;
            cookieMgr->SetCookie("session_token", sessionToken, cp);

            // POST a body that names user B's id and fresh field values.
            Json::JsonObject body;
            body["id"] = static_cast<int64_t>(bId);
            body["person_id"] = static_cast<int64_t>(bId);
            body["email"] = "attacker_picked@example.com";
            body["first_name"] = "Attacker";
            body["last_name"] = "Renamed";

            crow::request req;
            req.method = crow::HTTPMethod::POST;
            req.url = "/api/set_user_info";
            req.body = Json::Value(std::move(body)).ToString();
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
            EXPECT_EQ(resp.code, 200);

            // User A's row was updated (the session user, not the body's
            // id).
            Auth::PersonInfo aAfter;
            ASSERT_TRUE(personHelper.LookupPerson(transaction, aId, aAfter));
            EXPECT_EQ(aAfter.email, "attacker_picked@example.com");
            EXPECT_EQ(aAfter.firstName, "Attacker");
            EXPECT_EQ(aAfter.lastName, "Renamed");

            // User B's row is untouched.
            Auth::PersonInfo bAfter;
            ASSERT_TRUE(personHelper.LookupPerson(transaction, bId, bAfter));
            EXPECT_EQ(bAfter.email, bInfo.email);
            EXPECT_EQ(bAfter.firstName, bInfo.firstName);
            EXPECT_EQ(bAfter.lastName, bInfo.lastName);
        });

    Auth::ServerConfig::Shutdown();
}

} // namespace
} // namespace Endpoints