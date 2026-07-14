#include "resolve_fk_display.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "endpoints/endpoint_test_helper.h"
#include "web_app.h"
#include "db_schema/people.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/json/database_rest_helper.h"
#include "sql_util/table_helpers/admin_table_display_template.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/json_test_util.h"
#include "util/error_codes.h"
#include "util/json_value.h"

namespace Endpoints {
    namespace {

        void AddDisplayTemplate(
            Transaction& transaction, TestDatabaseUtil& testDb,
            std::string_view tableName, std::string_view displayTemplate) {
            TableHelpers::AdminTableDisplayTemplate helper(
                testDb.GetDatabaseHelper());
            helper.SetDisplayTemplate(transaction, tableName, displayTemplate);
        }

        std::string AddPersonReturnId(
            Transaction& transaction, TestDatabaseUtil& testDb,
            std::string_view email, std::string_view firstName,
            std::string_view lastName) {
            DatabaseRESTHelper restHelper(testDb.GetDatabaseHelper());
            Json::Value person = JsonTestUtil::JsonPerson(
                email, firstName, lastName, "hash");
            Json::Value result = restHelper.AddTableValueFetchPrimaryKey(
                transaction, testDb.GetDatabaseInfo(),
                DbSchema::kPeopleTable, person);
            return result["id"].Get<std::string>();
        }

        TEST(ResolveFkDisplayTest, ResolvesDisplayText) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("ResolvesDisplayText",
                [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                endpointHelper.AddAllowedTable(
                    transaction, DbSchema::kPeopleTable);
                AddDisplayTemplate(
                    transaction, testDb, DbSchema::kPeopleTable,
                    "{first_name} {last_name}");

                std::string aliceId = AddPersonReturnId(
                    transaction, testDb, "alice@test.com", "Alice", "Smith");
                std::string bobId = AddPersonReturnId(
                    transaction, testDb, "bob@test.com", "Bob", "Jones");

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/resolve_fk_display";
                req.body = R"({"parent_table_name": "people", "values": [")"
                    + aliceId + R"(", ")" + bobId + R"("]})";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);

                const auto& resolved = body["resolved"].GetChildren();
                EXPECT_EQ(resolved.at(aliceId).Get<std::string>(),
                    "Alice Smith");
                EXPECT_EQ(resolved.at(bobId).Get<std::string>(),
                    "Bob Jones");
            });
        }

        TEST(ResolveFkDisplayTest, EmptyValuesReturnsEmptyResolved) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("EmptyValuesReturnsEmptyResolved",
                [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                endpointHelper.AddAllowedTable(
                    transaction, DbSchema::kPeopleTable);

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/resolve_fk_display";
                req.body = R"({"parent_table_name": "people", "values": []})";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);

                const auto& resolved = body["resolved"].GetChildren();
                EXPECT_EQ(resolved.size(), 0);
            });
        }

        TEST(ResolveFkDisplayTest, TableNotAllowedReturnsError) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("TableNotAllowedReturnsError",
                [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                // Do NOT add people to allowed tables

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/resolve_fk_display";
                req.body = R"({"parent_table_name": "people", "values": ["1"]})";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                EXPECT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(),
                    ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(),
                    "Table(people) is not an allowed table.");
            });
        }

        TEST(ResolveFkDisplayTest, NoBodyReturnsError) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("NoBodyReturnsError",
                [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/resolve_fk_display";
                req.body = "";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                EXPECT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["detail"].Get<std::string>(),
                    "No message body.");
            });
        }

        TEST(ResolveFkDisplayTest, MissingValuesFieldReturnsError) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("MissingValuesFieldReturnsError",
                [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                endpointHelper.AddAllowedTable(
                    transaction, DbSchema::kPeopleTable);

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/resolve_fk_display";
                req.body = R"({"parent_table_name": "people"})";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                EXPECT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(),
                    ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(),
                    "values not found.");
            });
        }

        TEST(ResolveFkDisplayTest, NonexistentValuesOmittedFromResult) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("NonexistentValuesOmittedFromResult",
                [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                endpointHelper.AddAllowedTable(
                    transaction, DbSchema::kPeopleTable);
                AddDisplayTemplate(
                    transaction, testDb, DbSchema::kPeopleTable,
                    "{first_name} {last_name}");

                std::string aliceId = AddPersonReturnId(
                    transaction, testDb, "alice@test.com", "Alice", "Smith");

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/resolve_fk_display";
                req.body = R"({"parent_table_name": "people", "values": [")"
                    + aliceId + R"(", "99999"]})";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);

                const auto& resolved = body["resolved"].GetChildren();
                EXPECT_EQ(resolved.size(), 1);
                EXPECT_EQ(resolved.at(aliceId).Get<std::string>(),
                    "Alice Smith");
            });
        }

        TEST(ResolveFkDisplayTest, NoDisplayTemplateUsesPrimaryKey) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("NoDisplayTemplateUsesPrimaryKey",
                [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                endpointHelper.AddAllowedTable(
                    transaction, DbSchema::kPeopleTable);
                // No display template set

                std::string aliceId = AddPersonReturnId(
                    transaction, testDb, "alice@test.com", "Alice", "Smith");

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/resolve_fk_display";
                req.body = R"({"parent_table_name": "people", "values": [")"
                    + aliceId + R"("]})";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);

                const auto& resolved = body["resolved"].GetChildren();
                // With no template, display text should equal the pk value
                EXPECT_EQ(resolved.at(aliceId).Get<std::string>(), aliceId);
            });
        }

    }  // namespace
}  // namespace Endpoints
