#include "get_fk_options.h"

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

        using ::testing::UnorderedElementsAre;

        void AddDisplayTemplate(
            Transaction& transaction, TestDatabaseUtil& testDb,
            std::string_view tableName, std::string_view displayTemplate) {
            TableHelpers::AdminTableDisplayTemplate helper(
                testDb.GetDatabaseHelper());
            helper.SetDisplayTemplate(transaction, tableName, displayTemplate);
        }

        void AddPerson(
            Transaction& transaction, TestDatabaseUtil& testDb,
            std::string_view email, std::string_view firstName,
            std::string_view lastName) {
            DatabaseRESTHelper restHelper(testDb.GetDatabaseHelper());
            Json::Value person = JsonTestUtil::JsonPerson(
                email, firstName, lastName, "hash");
            restHelper.AddTableValueFetchPrimaryKey(
                transaction, testDb.GetDatabaseInfo(),
                DbSchema::kPeopleTable, person);
        }

        TEST(GetFkOptionsTest, EmptySearchReturnsAllRows) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("EmptySearchReturnsAllRows",
                [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                endpointHelper.AddAllowedTable(
                    transaction, DbSchema::kPeopleTable);
                AddDisplayTemplate(
                    transaction, testDb, DbSchema::kPeopleTable,
                    "{first_name} {last_name}");

                AddPerson(transaction, testDb,
                    "alice@test.com", "Alice", "Smith");
                AddPerson(transaction, testDb,
                    "bob@test.com", "Bob", "Jones");

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_fk_options";
                req.body = R"({"table_name": "people"})";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);

                EXPECT_EQ(body["total_count"].Get<int64_t>(), 2);
                const auto& options = body["options"].GetArray();
                ASSERT_EQ(options.size(), 2);

                // Verify each option has value and display fields
                for (const auto& option : options) {
                    EXPECT_FALSE(option["value"].Get<std::string>().empty());
                    EXPECT_FALSE(option["display"].Get<std::string>().empty());
                }
            });
        }

        TEST(GetFkOptionsTest, SearchTextFiltersResults) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("SearchTextFiltersResults",
                [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                endpointHelper.AddAllowedTable(
                    transaction, DbSchema::kPeopleTable);
                AddDisplayTemplate(
                    transaction, testDb, DbSchema::kPeopleTable,
                    "{first_name} {last_name}");

                AddPerson(transaction, testDb,
                    "alice@test.com", "Alice", "Smith");
                AddPerson(transaction, testDb,
                    "bob@test.com", "Bob", "Jones");
                AddPerson(transaction, testDb,
                    "charlie@test.com", "Charlie", "Smith");

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_fk_options";
                req.body = R"({"table_name": "people", "search_text": "alice"})";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);

                EXPECT_EQ(body["total_count"].Get<int64_t>(), 1);
                const auto& options = body["options"].GetArray();
                ASSERT_EQ(options.size(), 1);
                EXPECT_EQ(options[0]["display"].Get<std::string>(),
                    "Alice Smith");
            });
        }

        TEST(GetFkOptionsTest, ResultsIncludeResolvedDisplayText) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("ResultsIncludeResolvedDisplayText",
                [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                endpointHelper.AddAllowedTable(
                    transaction, DbSchema::kPeopleTable);
                AddDisplayTemplate(
                    transaction, testDb, DbSchema::kPeopleTable,
                    "{first_name} {last_name} - {email}");

                AddPerson(transaction, testDb,
                    "alice@test.com", "Alice", "Smith");

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_fk_options";
                req.body = R"({"table_name": "people"})";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);

                const auto& options = body["options"].GetArray();
                ASSERT_EQ(options.size(), 1);
                EXPECT_EQ(options[0]["display"].Get<std::string>(),
                    "Alice Smith - alice@test.com");
            });
        }

        TEST(GetFkOptionsTest, TableNotAllowedReturnsError) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("TableNotAllowedReturnsError",
                [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                // Do NOT add people to allowed tables

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_fk_options";
                req.body = R"({"table_name": "people"})";
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

        TEST(GetFkOptionsTest, NoBodyReturnsError) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("NoBodyReturnsError",
                [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_fk_options";
                req.body = "";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                EXPECT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["detail"].Get<std::string>(),
                    "No message body.");
            });
        }

        TEST(GetFkOptionsTest, PageSizeLimitsResults) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("PageSizeLimitsResults",
                [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                endpointHelper.AddAllowedTable(
                    transaction, DbSchema::kPeopleTable);
                AddDisplayTemplate(
                    transaction, testDb, DbSchema::kPeopleTable,
                    "{first_name} {last_name}");

                AddPerson(transaction, testDb,
                    "alice@test.com", "Alice", "Smith");
                AddPerson(transaction, testDb,
                    "bob@test.com", "Bob", "Jones");
                AddPerson(transaction, testDb,
                    "charlie@test.com", "Charlie", "Brown");

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_fk_options";
                req.body = R"({"table_name": "people", "page_size": 2})";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);

                // total_count reflects all matching rows, not page
                EXPECT_EQ(body["total_count"].Get<int64_t>(), 3);
                const auto& options = body["options"].GetArray();
                EXPECT_EQ(options.size(), 2);
            });
        }

        TEST(GetFkOptionsTest, NoDisplayTemplateUsesPrimaryKey) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("NoDisplayTemplateUsesPrimaryKey",
                [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                endpointHelper.AddAllowedTable(
                    transaction, DbSchema::kPeopleTable);
                // No display template set — should fall back to pk value

                AddPerson(transaction, testDb,
                    "alice@test.com", "Alice", "Smith");

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_fk_options";
                req.body = R"({"table_name": "people"})";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);

                const auto& options = body["options"].GetArray();
                ASSERT_EQ(options.size(), 1);
                // When no template, display should equal the primary key value
                EXPECT_EQ(options[0]["display"].Get<std::string>(),
                    options[0]["value"].Get<std::string>());
            });
        }

    }  // namespace
}  // namespace Endpoints
