#include "delete_item.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "endpoints/endpoint_test_helper.h"
#include "web_app.h"
#include "business_logic/auth/cookie_manager_test_util.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "db_schema/people.h"
#include "db_schema/permissions.h"
#include "db_schema/roles.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/table_helpers/permissions.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "sql_util/table_helpers/roles.h"
#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/error_codes.h"
#include "util/json_value.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"

namespace Endpoints {
    namespace
    {
        // The person the test's requests are signed in as.
        constexpr std::string_view kCallerEmail = "caller@example.com";

        // Signs this test's requests in as a new person. With `admin`, that
        // person holds the admin role. The admin role exists either way, as it
        // does in a real database: Session::IsAdmin looks it up by name.
        void SignIn(
            Transaction& transaction,
            TestDatabaseUtil& testDb,
            EndpointTestHelper& endpointHelper,
            bool admin,
            int64_t& outPersonId) {
            auto secrets = endpointHelper.GetSecretsHelper();
            secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
                std::to_string(15LL * 60LL * 1000000LL));

            Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
            Auth::PersonInfo info{ std::string(kCallerEmail), "Caller", "Person" };
            personHelper.CreateFullyValidatedUser(transaction, info, "Password123!");

            std::string sessionToken;
            ASSERT_TRUE(personHelper.CreateSessionToken(
                transaction, secrets, info.email, sessionToken));

            TableHelpers::People people(testDb.GetDatabaseHelper());
            KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
            outPersonId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

            TableHelpers::Roles roles(testDb.GetDatabaseHelper());
            int64_t adminRoleId = roles.AddRole(
                transaction, std::string(DbSchema::kRoleNameAdmin), "Administrator");
            if (admin) {
                TableHelpers::RoleAssignments roleAssignments(testDb.GetDatabaseHelper());
                roleAssignments.AddRoleAssignment(transaction, outPersonId, adminRoleId);
            }

            auto cookieManager = endpointHelper.GetCookieManagerTest();
            Auth::CookieProperties cookieProperties;
            cookieProperties.path = "/";
            cookieProperties.sameSite = Auth::CookieSameSitePolicy::None;
            cookieManager->SetCookie("session_token", sessionToken, cookieProperties);
        }

        // Gives the signed-in person a permission that grants `tableName`
        // through admin_table_permissions: the "whoever administers this table"
        // grant that RequireTableWriteAccess honours.
        void GrantTable(
            Transaction& transaction,
            TestDatabaseUtil& testDb,
            EndpointTestHelper& endpointHelper,
            int64_t personId,
            std::string_view tableName) {
            constexpr std::string_view kPermission = "author_things";
            endpointHelper.GrantPermissionToPerson(transaction, personId, kPermission);
            TableHelpers::Permissions permissions(testDb.GetDatabaseHelper());
            KeyValueTable row = permissions.GetPermission(transaction, kPermission);
            ASSERT_FALSE(row.empty());
            endpointHelper.AddAdminTablePermission(
                transaction, tableName,
                std::stoll(row.at(std::string(DbSchema::kPermissionsId))));
        }

        // Adds a person row directly (not through the endpoint under test) and
        // returns its id.
        std::string AddPersonRow(
            Transaction& transaction, DatabaseHelper databaseHelper,
            std::string_view email) {
            TableHelpers::People people(databaseHelper);
            Auth::PersonHelper personHelper(databaseHelper);
            Auth::PersonInfo info{ std::string(email), "first", "last" };
            personHelper.CreateFullyValidatedUser(transaction, info, "Password123!");
            KeyValueTable row = people.LookupPersonByEmail(transaction, email);
            return row.at(std::string(DbSchema::kPeopleId));
        }

        bool PersonExists(
            Transaction& transaction, DatabaseHelper databaseHelper,
            std::string_view email) {
            for (const auto& row : DbCrud::GetTableRows(
                     transaction, databaseHelper, DbSchema::kPeopleTable)) {
                if (row.at(std::string(DbSchema::kPeopleEmail)) == email) {
                    return true;
                }
            }
            return false;
        }

        crow::response SendDelete(
            EndpointTestHelper& endpointHelper, const std::string& id,
            crow::HTTPMethod method = crow::HTTPMethod::Post) {
            crow::request req;
            req.method = method;
            req.url = "/api/delete_item/people/id/" + id;
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
            return resp;
        }

        TEST(DeleteItemTest, DeleteItemBasic) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DeleteItemBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);
                int64_t callerId = 0;
                SignIn(transaction, testDatabaseUtil, endpointTestHelper, /*admin=*/true, callerId);

                const std::string id1 = AddPersonRow(transaction, databaseHelper, "email1");
                const std::string id2 = AddPersonRow(transaction, databaseHelper, "email2");

                ASSERT_EQ(SendDelete(endpointTestHelper, id2).code, 200);
                EXPECT_TRUE(PersonExists(transaction, databaseHelper, "email1"));
                EXPECT_FALSE(PersonExists(transaction, databaseHelper, "email2"));

                ASSERT_EQ(SendDelete(endpointTestHelper, id1).code, 200);
                EXPECT_FALSE(PersonExists(transaction, databaseHelper, "email1"));
                });
            Auth::ServerConfig::Shutdown();
        }

        // An admin still can't delete from a table that is on no list.
        TEST(DeleteItemTest, DeleteItemTableNotAllowed) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DeleteItemTableNotAllowed", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                int64_t callerId = 0;
                SignIn(transaction, testDatabaseUtil, endpointTestHelper, /*admin=*/true, callerId);
                const std::string id = AddPersonRow(transaction, databaseHelper, "email1");

                crow::response resp = SendDelete(endpointTestHelper, id);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(), "Table(people) is not an allowed table.");
                EXPECT_TRUE(PersonExists(transaction, databaseHelper, "email1"));
                });
            Auth::ServerConfig::Shutdown();
        }

        // Beta feedback 1.2: the public list lets anyone READ a table; it never
        // let a signed-out visitor delete from it, and now it doesn't.
        TEST(DeleteItemTest, SignedOutIsRefused) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DeleteItemSignedOut", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);
                const std::string id = AddPersonRow(transaction, databaseHelper, "email1");

                EXPECT_EQ(SendDelete(endpointTestHelper, id).code, 401);
                EXPECT_TRUE(PersonExists(transaction, databaseHelper, "email1"));
                });
            Auth::ServerConfig::Shutdown();
        }

        TEST(DeleteItemTest, WithoutGrantIsRefused) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DeleteItemWithoutGrant", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);
                int64_t callerId = 0;
                SignIn(transaction, testDatabaseUtil, endpointTestHelper, /*admin=*/false, callerId);
                const std::string id = AddPersonRow(transaction, databaseHelper, "email1");

                EXPECT_EQ(SendDelete(endpointTestHelper, id).code, 403);
                EXPECT_TRUE(PersonExists(transaction, databaseHelper, "email1"));
                });
            Auth::ServerConfig::Shutdown();
        }

        TEST(DeleteItemTest, WithGrantIsAllowed) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DeleteItemWithGrant", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                int64_t callerId = 0;
                SignIn(transaction, testDatabaseUtil, endpointTestHelper, /*admin=*/false, callerId);
                GrantTable(transaction, testDatabaseUtil, endpointTestHelper, callerId,
                           DbSchema::kPeopleTable);
                const std::string id = AddPersonRow(transaction, databaseHelper, "email1");

                EXPECT_EQ(SendDelete(endpointTestHelper, id).code, 200);
                EXPECT_FALSE(PersonExists(transaction, databaseHelper, "email1"));
                });
            Auth::ServerConfig::Shutdown();
        }

        // A grant on one table is not a grant on another.
        TEST(DeleteItemTest, GrantIsPerTable) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DeleteItemGrantIsPerTable", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);
                int64_t callerId = 0;
                SignIn(transaction, testDatabaseUtil, endpointTestHelper, /*admin=*/false, callerId);
                GrantTable(transaction, testDatabaseUtil, endpointTestHelper, callerId,
                           DbSchema::kRolesTable);
                const std::string id = AddPersonRow(transaction, databaseHelper, "email1");

                EXPECT_EQ(SendDelete(endpointTestHelper, id).code, 403);
                EXPECT_TRUE(PersonExists(transaction, databaseHelper, "email1"));
                });
            Auth::ServerConfig::Shutdown();
        }

        // Beta feedback 1.2: the route is POST only. As a GET it skipped the CSRF
        // guard, so a link on another site could delete a row with an admin's
        // cookie. A GET now never reaches the handler, even an admin's.
        TEST(DeleteItemTest, GetIsNotAllowed) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DeleteItemGet", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);
                int64_t callerId = 0;
                SignIn(transaction, testDatabaseUtil, endpointTestHelper, /*admin=*/true, callerId);
                const std::string id = AddPersonRow(transaction, databaseHelper, "email1");

                crow::response resp =
                    SendDelete(endpointTestHelper, id, crow::HTTPMethod::Get);

                EXPECT_EQ(resp.code, 405);
                EXPECT_TRUE(PersonExists(transaction, databaseHelper, "email1"));
                });
            Auth::ServerConfig::Shutdown();
        }

    } // namespace
}  // namespace Endpoints
