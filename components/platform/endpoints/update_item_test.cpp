#include "update_item.h"

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
#include "test/src/util/database_test_helper.h"
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
        // through admin_table_permissions.
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

        std::string RenameBody(const std::string& id, std::string_view newFirstName) {
            Json::JsonObject value;
            value[std::string(DbSchema::kPeopleFirstName)] = std::string(newFirstName);
            Json::JsonObject updateItem;
            updateItem["table_name"] = std::string(DbSchema::kPeopleTable);
            updateItem["column_name"] = std::string(DbSchema::kPeopleId);
            updateItem["column_value"] = id;
            updateItem["value"] = Json::Value(std::move(value));
            return Json::Value(std::move(updateItem)).ToString();
        }

        std::string FirstNameOf(
            Transaction& transaction, DatabaseHelper databaseHelper,
            std::string_view email) {
            TableHelpers::People people(databaseHelper);
            return people.LookupPersonByEmail(transaction, email)
                .at(std::string(DbSchema::kPeopleFirstName));
        }

        TEST(UpdateItemTest, UpdateItemBasic) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("UpdateItemBasic", [&](Transaction& transaction) {
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

				req.body = Json::Value(std::move(addItem2)).ToString();
                resp.clear();
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                // Get actual IDs from inserted rows
                auto rows = PeopleOtherThanCaller(transaction, databaseHelper);
                ASSERT_EQ(rows.size(), 2u);
                std::string id1;
                for (const auto& row : rows) {
                    if (row.at(std::string(DbSchema::kPeopleEmail)) == "email1") {
                        id1 = row.at(std::string(DbSchema::kPeopleId));
                    }
                }
                ASSERT_FALSE(id1.empty());

				Json::Value person3 = JsonPerson(
                    "email3", "first3", "last3", "hash3");
				Json::JsonObject updateItem;
				updateItem["table_name"] = std::string(DbSchema::kPeopleTable);
				updateItem["column_name"] = "id";
				updateItem["column_value"] = id1;
				updateItem["value"] = std::move(person3);

                req.url = "/api/update_item";
				req.body = Json::Value(std::move(updateItem)).ToString();
                resp.clear();
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(
                    {
                        {
                            { std::string(DbSchema::kPeopleEmail), "email3" },
                            { std::string(DbSchema::kPeopleFirstName), "first3" },
                            { std::string(DbSchema::kPeopleLastName), "last3" },
                            { std::string(DbSchema::kPeoplePasswordHash), "hash3" }
                        },
                        {
                            { std::string(DbSchema::kPeopleEmail), "email2" },
                            { std::string(DbSchema::kPeopleFirstName), "first2" },
                            { std::string(DbSchema::kPeopleLastName), "last2" },
                            { std::string(DbSchema::kPeoplePasswordHash), "hash2" }
                        }
                    },
                    PeopleOtherThanCaller(transaction, databaseHelper),
                    DbSchema::kPeopleId, DbSchema::kPeopleCreatedAt, DbSchema::kPeopleUpdatedAt, DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
                });
            Auth::ServerConfig::Shutdown();
        }

        TEST(UpdateItemTest, UpdateItemNoMessageBody) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("UpdateItemNoMessageBody", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

                crow::request req;
                req.url = "/api/update_item";
                req.method = crow::HTTPMethod::POST;
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kBadRequest);
                EXPECT_EQ(body["detail"].Get<std::string>(), "No message body.");
                });
        }

        TEST(UpdateItemTest, UpdateItemNoTableName) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("UpdateItemNoTableName", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

				Json::Value person3 = JsonPerson(
                    "email3", "first3", "last3", "hash3");
				Json::JsonObject updateItem;
				updateItem["column_name"] = "id";
				updateItem["column_value"] = "1";
				updateItem["value"] = std::move(person3);

                crow::request req;
                req.url = "/api/update_item";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(updateItem)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(), "table_name not found.");
                });
        }

        TEST(UpdateItemTest, UpdateItemNoValue) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("UpdateItemNoValue", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

				Json::JsonObject updateItem;
				updateItem["table_name"] = std::string(DbSchema::kPeopleTable);
				updateItem["column_name"] = "id";
				updateItem["column_value"] = "1";

                crow::request req;
                req.url = "/api/update_item";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(updateItem)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(), "value not found.");
                });
        }

        TEST(UpdateItemTest, UpdateItemNoColumnName) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("UpdateItemNoColumnName", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

				Json::Value person3 = JsonPerson(
                    "email3", "first3", "last3", "hash3");
				Json::JsonObject updateItem;
				updateItem["table_name"] = std::string(DbSchema::kPeopleTable);
				updateItem["column_value"] = "1";
				updateItem["value"] = std::move(person3);

                crow::request req;
                req.url = "/api/update_item";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(updateItem)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(), "column_name not found.");
                });
        }

        TEST(UpdateItemTest, UpdateItemNoColumnValue) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("UpdateItemNoColumnValue", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

				Json::Value person3 = JsonPerson(
                    "email3", "first3", "last3", "hash3");
				Json::JsonObject updateItem;
				updateItem["table_name"] = std::string(DbSchema::kPeopleTable);
				updateItem["column_name"] = "id";
				updateItem["value"] = std::move(person3);

                crow::request req;
                req.url = "/api/update_item";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(updateItem)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(), "column_value not found.");
                });
        }

        // An admin still can't write to a table that is on no list.
        TEST(UpdateItemTest, UpdateItemTableNotAllowed) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("UpdateItemTableNotAllowed", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                int64_t personId = 0;
                SignIn(transaction, testDatabaseUtil, endpointTestHelper, true, personId);
                std::string targetId = AddPersonRow(
                    transaction, databaseHelper, "target@example.com");

                crow::request req;
                req.url = "/api/update_item";
                req.method = crow::HTTPMethod::POST;
				req.body = RenameBody(targetId, "Changed");
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(), "Table(people) is not an allowed table.");
                EXPECT_EQ(FirstNameOf(transaction, databaseHelper, "target@example.com"), "first");
                });
            Auth::ServerConfig::Shutdown();
        }

        // Beta feedback Phase 1.2: a base-list table is readable by anyone, but
        // changing a row needs a signed-in caller with a grant.
        TEST(UpdateItemTest, UpdateItemSignedOutIsRefused) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("UpdateItemSignedOutIsRefused", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);
                std::string targetId = AddPersonRow(
                    transaction, databaseHelper, "target@example.com");

                crow::request req;
                req.url = "/api/update_item";
                req.method = crow::HTTPMethod::Post;
                req.body = RenameBody(targetId, "Changed");
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                EXPECT_EQ(resp.code, 401);
                EXPECT_EQ(FirstNameOf(transaction, databaseHelper, "target@example.com"), "first");
                });
        }

        TEST(UpdateItemTest, UpdateItemWithoutGrantIsRefused) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("UpdateItemWithoutGrantIsRefused", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);
                int64_t personId = 0;
                SignIn(transaction, testDatabaseUtil, endpointTestHelper, false, personId);
                std::string targetId = AddPersonRow(
                    transaction, databaseHelper, "target@example.com");

                crow::request req;
                req.url = "/api/update_item";
                req.method = crow::HTTPMethod::Post;
                req.body = RenameBody(targetId, "Changed");
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                EXPECT_EQ(resp.code, 403);
                EXPECT_EQ(FirstNameOf(transaction, databaseHelper, "target@example.com"), "first");
                });
            Auth::ServerConfig::Shutdown();
        }

        TEST(UpdateItemTest, UpdateItemWithGrantIsAllowed) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("UpdateItemWithGrantIsAllowed", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                int64_t personId = 0;
                SignIn(transaction, testDatabaseUtil, endpointTestHelper, false, personId);
                GrantTable(transaction, testDatabaseUtil, endpointTestHelper,
                    personId, DbSchema::kPeopleTable);
                std::string targetId = AddPersonRow(
                    transaction, databaseHelper, "target@example.com");

                crow::request req;
                req.url = "/api/update_item";
                req.method = crow::HTTPMethod::Post;
                req.body = RenameBody(targetId, "Changed");
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                EXPECT_EQ(resp.code, 200) << resp.body;
                EXPECT_EQ(FirstNameOf(transaction, databaseHelper, "target@example.com"), "Changed");
                });
            Auth::ServerConfig::Shutdown();
        }

    } // namespace
}  // namespace Endpoints
