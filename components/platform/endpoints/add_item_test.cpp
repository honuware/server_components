#include "add_item.h"

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
#include "sql_util/table_helpers/admin_table_permissions.h"
#include "sql_util/table_helpers/allowed_tables.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/table_helpers/permissions.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "sql_util/table_helpers/roles.h"
#include "sql_util/database_common.h"
#include "sql_util/json/database_rest_helper.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/json_test_util.h"
#include "test/src/util/json_value_matcher.h"
#include "test/src/util/test_helper.h"
#include "test/src/util/table_matcher.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/error_codes.h"
#include "util/json_value.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"

namespace Endpoints {
    namespace
    {
        using Json::JsonPair;
        using Json::JsonPairValue;
        using JsonTestUtil::JsonPerson;

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

        // Every people row except the signed-in caller's own.
        KeyValueTableArray PeopleOtherThanCaller(
            Transaction& transaction, DatabaseHelper databaseHelper) {
            KeyValueTableArray result;
            for (const auto& row : DbCrud::GetTableRows(
                     transaction, databaseHelper, DbSchema::kPeopleTable)) {
                if (row.at(std::string(DbSchema::kPeopleEmail)) != kCallerEmail) {
                    result.push_back(row);
                }
            }
            return result;
        }

        std::string AddPersonBody(std::string_view email) {
            Json::JsonObject addItem;
            addItem["table_name"] = std::string(DbSchema::kPeopleTable);
            addItem["value"] = JsonPerson(email, "first", "last", "hash");
            return Json::Value(std::move(addItem)).ToString();
        }

        TEST(AddItemTest, AddItemBasic) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);
                int64_t personId = 0;
                SignIn(transaction, testDatabaseUtil, endpointTestHelper, true, personId);

				Json::Value person1 = JsonPerson(
                    "email1", "first1", "last1", "hash1");
				Json::Value person2 = JsonPerson(
                    "email2", "first2", "last2", "hash2");

				Json::JsonObject addItem1;
				addItem1["table_name"] = std::string(DbSchema::kPeopleTable);
				addItem1["value"] = std::move(person1);
				Json::JsonObject addItem2;
				addItem2["table_name"] = std::string(DbSchema::kPeopleTable);
				addItem2["value"] = std::move(person2);

                crow::request req;
                req.url = "/api/add_item";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(addItem1)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);

                ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(
                    {
                        { {"email", "email1"}, {"first_name", "first1"}, {"last_name", "last1"}, {"password_hash", "hash1"} }
                    },
                    PeopleOtherThanCaller(transaction, databaseHelper),
                    DbSchema::kPeopleId, DbSchema::kPeopleCreatedAt, DbSchema::kPeopleUpdatedAt, DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
				req.body = Json::Value(std::move(addItem2)).ToString();
                resp.clear();
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);

                ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(
                    {
                        { {"email", "email1"}, {"first_name", "first1"}, {"last_name", "last1"}, {"password_hash", "hash1"} },
                        { {"email", "email2"}, {"first_name", "first2"}, {"last_name", "last2"}, {"password_hash", "hash2"} }
                    },
                    PeopleOtherThanCaller(transaction, databaseHelper),
                    DbSchema::kPeopleId, DbSchema::kPeopleCreatedAt, DbSchema::kPeopleUpdatedAt, DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
                });
            Auth::ServerConfig::Shutdown();
        }

        TEST(AddItemTest, AddItemNoMessageBody) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemNoMessageBody", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

                crow::request req;
                req.url = "/api/add_item";
                req.method = crow::HTTPMethod::POST;
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kBadRequest);
                EXPECT_EQ(body["detail"].Get<std::string>(), "No message body.");
                });
        }

        TEST(AddItemTest, AddItemNoTableName) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemNoTableName", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

				Json::Value person1 = JsonPerson(
                    "email1", "first1", "last1", "hash1");
				Json::JsonObject addItem1;
				addItem1["value"] = std::move(person1);

                crow::request req;
                req.url = "/api/add_item";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(addItem1)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(), "table_name not found.");
                });
        }

        TEST(AddItemTest, AddItemNoValue) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemNoValue", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

				Json::JsonObject addItem1;
				addItem1["table_name"] = std::string(DbSchema::kPeopleTable);

                crow::request req;
                req.url = "/api/add_item";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(addItem1)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(), "value not found.");
                });
        }

        // An admin still can't write to a table that is on no list.
        TEST(AddItemTest, AddItemTableNotAllowed) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemTableNotAllowed", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                int64_t personId = 0;
                SignIn(transaction, testDatabaseUtil, endpointTestHelper, true, personId);

				Json::Value person1 = JsonPerson(
                    "email1", "first1", "last1", "hash1");
				Json::JsonObject addItem1;
				addItem1["table_name"] = std::string(DbSchema::kPeopleTable);
				addItem1["value"] = std::move(person1);

                crow::request req;
                req.url = "/api/add_item";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(addItem1)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(), "Table(people) is not an allowed table.");
                EXPECT_TRUE(PeopleOtherThanCaller(transaction, databaseHelper).empty());
                });
            Auth::ServerConfig::Shutdown();
        }

        // Beta feedback Phase 1.2: the app's base list makes a table readable by
        // anyone. It used to make it writable by anyone too, signed in or not.
        TEST(AddItemTest, AddItemSignedOutIsRefused) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemSignedOutIsRefused", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

                crow::request req;
                req.url = "/api/add_item";
                req.method = crow::HTTPMethod::Post;
                req.body = AddPersonBody("intruder@example.com");
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                EXPECT_EQ(resp.code, 401);
                EXPECT_TRUE(DbCrud::GetTableRows(
                    transaction, databaseHelper, DbSchema::kPeopleTable).empty());
                });
        }

        // Signed in, but nothing grants the table: reading it isn't writing it.
        TEST(AddItemTest, AddItemWithoutGrantIsRefused) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemWithoutGrantIsRefused", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);
                int64_t personId = 0;
                SignIn(transaction, testDatabaseUtil, endpointTestHelper, false, personId);

                crow::request req;
                req.url = "/api/add_item";
                req.method = crow::HTTPMethod::Post;
                req.body = AddPersonBody("intruder@example.com");
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                EXPECT_EQ(resp.code, 403);
                EXPECT_TRUE(PeopleOtherThanCaller(transaction, databaseHelper).empty());
                });
            Auth::ServerConfig::Shutdown();
        }

        // A permission that grants the table through admin_table_permissions
        // lets a non-admin write it.
        TEST(AddItemTest, AddItemWithGrantIsAllowed) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemWithGrantIsAllowed", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                int64_t personId = 0;
                SignIn(transaction, testDatabaseUtil, endpointTestHelper, false, personId);
                GrantTable(transaction, testDatabaseUtil, endpointTestHelper,
                    personId, DbSchema::kPeopleTable);

                crow::request req;
                req.url = "/api/add_item";
                req.method = crow::HTTPMethod::Post;
                req.body = AddPersonBody("new@example.com");
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                EXPECT_EQ(resp.code, 200) << resp.body;
                KeyValueTableArray added = PeopleOtherThanCaller(transaction, databaseHelper);
                ASSERT_EQ(added.size(), 1u);
                EXPECT_EQ(added[0].at(std::string(DbSchema::kPeopleEmail)), "new@example.com");
                });
            Auth::ServerConfig::Shutdown();
        }

        // The grant is per table: a permission that unlocks one table says
        // nothing about another, even one the base list makes readable.
        TEST(AddItemTest, AddItemGrantIsPerTable) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemGrantIsPerTable", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);
                int64_t personId = 0;
                SignIn(transaction, testDatabaseUtil, endpointTestHelper, false, personId);
                GrantTable(transaction, testDatabaseUtil, endpointTestHelper,
                    personId, "roles");

                crow::request req;
                req.url = "/api/add_item";
                req.method = crow::HTTPMethod::Post;
                req.body = AddPersonBody("intruder@example.com");
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                EXPECT_EQ(resp.code, 403);
                EXPECT_TRUE(PeopleOtherThanCaller(transaction, databaseHelper).empty());
                });
            Auth::ServerConfig::Shutdown();
        }

    } // namespace
}  // namespace Endpoints
