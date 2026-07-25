#include "get_user_info.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <regex>

#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/auth/session.h"
#include "db_schema/people.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/table_helpers/people.h"
#include "util/secrets/secret_keys.h"
#include "test/src/util/database_test_helper.h"
#include "business_logic/auth/cookie_manager_test_util.h"
#include "util/error_codes.h"
#include "util/json_value.h"
#include "util/types.h"
#include "util/secrets/secrets_helper_test_util.h"

namespace Endpoints {
namespace {

using ::testing::HasSubstr;
using ::testing::Eq;

TEST(GetUserInfoTest, GetUserInfoBasic) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetUserInfoBasic", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL)); // 15 minutes

        // Create a fully validated user
        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "info_user@example.com", "First", "Last" };
        personHelper.CreateFullyValidatedUser(transaction, info, "MyPassword!");

        // Create roles
        TableHelpers::Roles roles(testDb.GetDatabaseHelper());
        int64_t ridViewer = roles.AddRole(transaction, "viewer", "Viewer");
        int64_t ridEditor = roles.AddRole(transaction, "editor", "Editor");

        // Create permissions
        TableHelpers::Permissions perms(testDb.GetDatabaseHelper());
        int64_t pidRead = perms.AddPermission(transaction, "read", "Read");
        int64_t pidWrite = perms.AddPermission(transaction, "write", "Write");

        // Map role->permissions
        TableHelpers::RolePermissions rperms(testDb.GetDatabaseHelper());
        rperms.AddRolePermission(transaction, ridViewer, pidRead);
        rperms.AddRolePermission(transaction, ridEditor, pidWrite);

        // Assign roles to user
        // Lookup person id to assign roles
        TableHelpers::People peopleTH(testDb.GetDatabaseHelper());
        KeyValueTable person = peopleTH.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());
        int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));
        TableHelpers::RoleAssignments ra(testDb.GetDatabaseHelper());
        ra.AddRoleAssignment(transaction, personId, ridViewer);
        ra.AddRoleAssignment(transaction, personId, ridEditor);

        // Create a session token and seed cookie
        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, info.email, sessionToken));
        ASSERT_FALSE(sessionToken.empty());

        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        // Call endpoint
        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/get_user_info";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);

        // Validate JSON body via Json::Value
        Json::Value body = Json::Value::FromText(resp.body);
        ASSERT_TRUE(body.HasChildren());
        EXPECT_EQ(body["email"].Get<std::string>(), "info_user@example.com");
        EXPECT_EQ(body["first_name"].Get<std::string>(), "First");
        EXPECT_EQ(body["last_name"].Get<std::string>(), "Last");

        // Validate created_at (e.g. "January 8, 2026")
        ASSERT_TRUE(body.HasChild("created_at", nullptr));
        const std::string createdAt = body["created_at"].Get<std::string>();
        ASSERT_FALSE(createdAt.empty());

        const std::regex kCreatedAtRegex(
            R"(^(January|February|March|April|May|June|July|August|September|October|November|December) ([1-9]|[12][0-9]|3[01]), 20\d{2}$)");

        EXPECT_TRUE(std::regex_match(createdAt, kCreatedAtRegex))
            << "Unexpected created_at display format: " << createdAt;

        // roles should contain viewer and editor
        const Json::Value& rolesVal = body["roles"];
        ASSERT_TRUE(rolesVal.IsArray());
        StringSet rolesSet;
        for (const auto& v : rolesVal.GetArray()) {
            rolesSet.insert(v.Get<std::string>());
        }
        EXPECT_TRUE(SetContains(rolesSet, "viewer"));
        EXPECT_TRUE(SetContains(rolesSet, "editor"));

		// permissions should contain read and write
		const Json::Value& permsVal = body["permissions"];
		ASSERT_TRUE(permsVal.IsArray());
		StringSet permsSet;
		for (const auto& v : permsVal.GetArray()) {
			permsSet.insert(v.Get<std::string>());
		}
        EXPECT_TRUE(SetContains(permsSet, "read"));
        EXPECT_TRUE(SetContains(permsSet, "write"));

        // must_change_password should default to false
        ASSERT_TRUE(body.HasChild("must_change_password", nullptr));
        EXPECT_EQ(body["must_change_password"].Get<bool>(), false);
    });

    Auth::ServerConfig::Shutdown();
}

TEST(GetUserInfoTest, GetUserInfoMustChangePassword) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetUserInfoMustChangePassword", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL));

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "mustchange@example.com", "Must", "Change" };
        personHelper.CreateFullyValidatedUser(transaction, info, "TempPass123!");

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

        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, info.email, sessionToken));

        auto cookieMgr = endpointHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/get_user_info";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);

        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["must_change_password"].Get<bool>(), true);
    });

    Auth::ServerConfig::Shutdown();
}

TEST(GetUserInfoTest, GetUserInfoNotLoggedIn) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetUserInfoNotLoggedIn", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        // Do NOT set a session_token cookie (user not logged in)

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/get_user_info";
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