#include "get_row_by_values.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "endpoints/endpoint_test_helper.h"
#include "web_app.h"
#include "db_schema/people.h"
#include "sql_util/table_helpers/admin_column_redactions.h"
#include "sql_util/table_helpers/allowed_tables.h"
#include "sql_util/database_common.h"
#include "sql_util/json/database_rest_helper.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/json_test_util.h"
#include "test/src/util/json_value_matcher.h"
#include "test/src/util/test_helper.h"
#include "util/error_codes.h"
#include "util/json_value.h"

namespace Endpoints {
    namespace
    {
        using Json::JsonPair;
        using JsonTestUtil::JsonPerson;

        std::string MakeRequestBody(
            std::string_view tableName,
            const KeyValueTable& filterPairs) {
            Json::JsonObject body;
            body["table_name"] = std::string(tableName);

            Json::JsonArray filters;
            for (const auto& [key, value] : filterPairs) {
                Json::JsonObject pair;
                pair["column_name"] = key;
                pair["column_value"] = value;
                filters.push_back(Json::Value(std::move(pair)));
            }
            body["filter_pairs"] = Json::Value(std::move(filters));

            return Json::Value(std::move(body)).ToString();
        }

        int64_t IdFromPrimaryKey(const Json::Value& value) {
            return std::stoll(value["id"].Get<std::string>());
        }

        constexpr std::string_view kRowResult = R"JSON(
{
    "sortedColumnNames": [
        "email",
        "first_name",
        "id",
        "last_name",
        "password_hash"
    ],
    "dataTable": [
        ["person2@hotmail.com", "first2", "{id2}", "last2", "hash2"]
    ]
}
)JSON";

        TEST(GetRowByValuesTest, FetchByPrimaryKey) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("FetchByPrimaryKey", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                DbSchema::DatabaseInfo databaseInfo = testDatabaseUtil.GetDatabaseInfo();

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

                DatabaseRESTHelper databaseRestHelper(databaseHelper);
                IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person1@hotmail.com", "first1", "last1", "hash1")));
                int64_t id2 = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person2@hotmail.com", "first2", "last2", "hash2")));

                KeyValueTable filterPairs = {
                    {"id", StringFromInt(id2)}
                };

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_row_by_values";
                req.body = MakeRequestBody("people", filterPairs);
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);

                KeyValueTable formatReplace = {
                    {"id2", StringFromInt(id2)}
                };
                ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                    Json::Value::FromText(resp.body),
                    Json::Value::FromText(FormatString(kRowResult, formatReplace)),
                    DbSchema::kPeopleCreatedAt,
                    DbSchema::kPeopleUpdatedAt,
                    DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
                });
        }

        constexpr std::string_view kRowByMultipleValues = R"JSON(
{
    "sortedColumnNames": [
        "email",
        "first_name",
        "id",
        "last_name",
        "password_hash"
    ],
    "dataTable": [
        ["person3@hotmail.com", "first3", "{id3}", "shared_last", "hash3"]
    ]
}
)JSON";

        TEST(GetRowByValuesTest, FetchByMultipleValues) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("FetchByMultipleValues", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                DbSchema::DatabaseInfo databaseInfo = testDatabaseUtil.GetDatabaseInfo();

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

                DatabaseRESTHelper databaseRestHelper(databaseHelper);
                IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person1@hotmail.com", "first1", "shared_last", "hash1")));
                IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person2@hotmail.com", "first2", "other_last", "hash2")));
                int64_t id3 = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person3@hotmail.com", "first3", "shared_last", "hash3")));

                KeyValueTable filterPairs = {
                    {"last_name", "shared_last"},
                    {"first_name", "first3"}
                };

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_row_by_values";
                req.body = MakeRequestBody("people", filterPairs);
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);

                KeyValueTable formatReplace = {
                    {"id3", StringFromInt(id3)}
                };
                ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                    Json::Value::FromText(resp.body),
                    Json::Value::FromText(FormatString(kRowByMultipleValues, formatReplace)),
                    DbSchema::kPeopleCreatedAt,
                    DbSchema::kPeopleUpdatedAt,
                    DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
                });
        }

        // Phase 3.8 of the security review: end-to-end check that
        // /api/get_row_by_values for a (people, ...) lookup never
        // returns the password_hash column.
        TEST(GetRowByValuesTest, FetchPeopleHidesPasswordHash) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction(
                "FetchPeopleHidesPasswordHash",
                [&](Transaction& transaction) {

                // Insert the redaction BEFORE constructing
                // EndpointTestHelper (the helper snapshots the redact
                // set into the WebApp at construction time).
                TableHelpers::AdminColumnRedactions redactions(
                    testDatabaseUtil.GetDatabaseHelper());
                redactions.AddAdminColumnRedaction(
                    transaction,
                    DbSchema::kPeopleTable,
                    DbSchema::kPeoplePasswordHash);

                DatabaseHelper databaseHelper =
                    testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(
                    transaction, testDatabaseUtil);
                DbSchema::DatabaseInfo databaseInfo =
                    testDatabaseUtil.GetDatabaseInfo();

                endpointTestHelper.AddAllowedTable(
                    transaction, DbSchema::kPeopleTable);

                DatabaseRESTHelper databaseRestHelper(databaseHelper);
                int64_t id = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson(
                            "byvals-redact@example.com", "first", "last",
                            "byvals-hash")));

                KeyValueTable filterPairs = {
                    {"id", StringFromInt(id)}
                };

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_row_by_values";
                req.body = MakeRequestBody("people", filterPairs);
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);
                ASSERT_TRUE(body.HasChild("sortedColumnNames", nullptr));
                for (const auto& col : body["sortedColumnNames"].GetArray()) {
                    EXPECT_NE(col.Get<std::string>(), "password_hash")
                        << "password_hash leaked through "
                           "/api/get_row_by_values for table=people";
                }
                // Cell-level scan: no cell should hold the password
                // value we inserted.
                for (const auto& row : body["dataTable"].GetArray()) {
                    for (const auto& cell : row.GetArray()) {
                        std::string s = cell.Is<std::string>()
                            ? cell.Get<std::string>()
                            : cell.ToSimpleString();
                        EXPECT_NE(s, "byvals-hash");
                    }
                }
            });
        }

        TEST(GetRowByValuesTest, NotAllowed) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("NotAllowed", [&](Transaction& transaction) {
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                KeyValueTable filterPairs = {
                    {"id", "1"}
                };

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_row_by_values";
                req.body = MakeRequestBody("people", filterPairs);
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(),
                    "Table(people) is not an allowed table.");
                });
        }

    } // namespace
}  // namespace Endpoints
