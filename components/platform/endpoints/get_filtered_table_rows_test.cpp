#include "get_filtered_table_rows.h"

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
            std::string_view columnName,
            bool asc,
            int pageSize,
            int page,
            const KeyValueTable& filterPairs = {}) {
            Json::JsonObject body;
            body["table_name"] = std::string(tableName);
            body["column_name"] = std::string(columnName);
            body["asc"] = asc;
            body["page_size"] = static_cast<int64_t>(pageSize);
            body["page"] = static_cast<int64_t>(page);

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

        constexpr std::string_view kNoFilterPage1 = R"JSON(
{
    "sortedColumnNames": [
        "email",
        "first_name",
        "id",
        "last_name",
        "password_hash"
    ],
    "dataTable": [
        ["person1@hotmail.com", "first1", "{id1}", "last1", "hash1"],
        ["person2@hotmail.com", "first2", "{id2}", "last2", "hash2"]
    ],
    "totalCount": 5
}
)JSON";

        constexpr std::string_view kNoFilterPage2 = R"JSON(
{
    "sortedColumnNames": [
        "email",
        "first_name",
        "id",
        "last_name",
        "password_hash"
    ],
    "dataTable": [
        ["person3@hotmail.com", "first3", "{id3}", "last3", "hash3"],
        ["person4@hotmail.com", "first4", "{id4}", "last4", "hash4"]
    ],
    "totalCount": 5
}
)JSON";

        TEST(GetFilteredTableRowsTest, NoFilters) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("NoFilters", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                DbSchema::DatabaseInfo databaseInfo = testDatabaseUtil.GetDatabaseInfo();

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

                DatabaseRESTHelper databaseRestHelper(databaseHelper);
                int64_t id1 = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person1@hotmail.com", "first1", "last1", "hash1")));
                int64_t id2 = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person2@hotmail.com", "first2", "last2", "hash2")));
                int64_t id3 = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person3@hotmail.com", "first3", "last3", "hash3")));
                int64_t id4 = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person4@hotmail.com", "first4", "last4", "hash4")));
                IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person5@hotmail.com", "first5", "last5", "hash5")));

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_filtered_table_rows";
                req.body = MakeRequestBody("people", "id", true, 2, 0);
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["totalCount"].Get<int64_t>(), 5);

                KeyValueTable formatReplace = {
                    {"id1", StringFromInt(id1)},
                    {"id2", StringFromInt(id2)}
                };
                ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                    body,
                    Json::Value::FromText(FormatString(kNoFilterPage1, formatReplace)),
                    DbSchema::kPeopleCreatedAt,
                    DbSchema::kPeopleUpdatedAt,
                    DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));

                // Verify second page
                resp.clear();
                req.body = MakeRequestBody("people", "id", true, 2, 1);
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["totalCount"].Get<int64_t>(), 5);

                KeyValueTable formatReplace2 = {
                    {"id3", StringFromInt(id3)},
                    {"id4", StringFromInt(id4)}
                };
                ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                    body,
                    Json::Value::FromText(FormatString(kNoFilterPage2, formatReplace2)),
                    DbSchema::kPeopleCreatedAt,
                    DbSchema::kPeopleUpdatedAt,
                    DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
                });
        }

        constexpr std::string_view kFilteredResult = R"JSON(
{
    "sortedColumnNames": [
        "email",
        "first_name",
        "id",
        "last_name",
        "password_hash"
    ],
    "dataTable": [
        ["person1@hotmail.com", "first1", "{id1}", "shared_last", "hash1"],
        ["person3@hotmail.com", "first3", "{id3}", "shared_last", "hash3"]
    ],
    "totalCount": 3
}
)JSON";

        TEST(GetFilteredTableRowsTest, SingleFilter) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("SingleFilter", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                DbSchema::DatabaseInfo databaseInfo = testDatabaseUtil.GetDatabaseInfo();

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

                DatabaseRESTHelper databaseRestHelper(databaseHelper);
                int64_t id1 = IdFromPrimaryKey(
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
                IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person4@hotmail.com", "first4", "another_last", "hash4")));
                IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person5@hotmail.com", "first5", "shared_last", "hash5")));

                KeyValueTable filterPairs = {
                    {"last_name", "shared_last"}
                };

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_filtered_table_rows";
                req.body = MakeRequestBody("people", "id", true, 2, 0, filterPairs);
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["totalCount"].Get<int64_t>(), 3);

                KeyValueTable formatReplace = {
                    {"id1", StringFromInt(id1)},
                    {"id3", StringFromInt(id3)}
                };
                ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                    body,
                    Json::Value::FromText(FormatString(kFilteredResult, formatReplace)),
                    DbSchema::kPeopleCreatedAt,
                    DbSchema::kPeopleUpdatedAt,
                    DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
                });
        }

        constexpr std::string_view kFilteredPage2 = R"JSON(
{
    "sortedColumnNames": [
        "email",
        "first_name",
        "id",
        "last_name",
        "password_hash"
    ],
    "dataTable": [
        ["person5@hotmail.com", "first5", "{id5}", "shared_last", "hash5"]
    ],
    "totalCount": 3
}
)JSON";

        TEST(GetFilteredTableRowsTest, FilteredPagination) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("FilteredPagination", [&](Transaction& transaction) {
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
                IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person3@hotmail.com", "first3", "shared_last", "hash3")));
                IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person4@hotmail.com", "first4", "another_last", "hash4")));
                int64_t id5 = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson("person5@hotmail.com", "first5", "shared_last", "hash5")));

                KeyValueTable filterPairs = {
                    {"last_name", "shared_last"}
                };

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_filtered_table_rows";
                req.body = MakeRequestBody("people", "id", true, 2, 1, filterPairs);
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["totalCount"].Get<int64_t>(), 3);

                KeyValueTable formatReplace = {
                    {"id5", StringFromInt(id5)}
                };
                ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                    body,
                    Json::Value::FromText(FormatString(kFilteredPage2, formatReplace)),
                    DbSchema::kPeopleCreatedAt,
                    DbSchema::kPeopleUpdatedAt,
                    DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
                });
        }

        TEST(GetFilteredTableRowsTest, NotAllowed) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("NotAllowed", [&](Transaction& transaction) {
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                // Do NOT add table to allowed tables

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_filtered_table_rows";
                req.body = MakeRequestBody("people", "id", true, 2, 0);
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(),
                    "Table(people) is not an allowed table.");
                });
        }

        // Phase 3.8 of the security review: end-to-end check that
        // /api/get_filtered_table_rows for a (people, ...) query
        // never returns the password_hash column. Exercises both the
        // no-filter and the filter-pair code paths, since
        // GetRowsByValues handles both.
        TEST(GetFilteredTableRowsTest, FilteredPeopleHidesPasswordHash) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction(
                "FilteredPeopleHidesPasswordHash",
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
                IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson(
                            "filt-1@example.com", "first1", "shared",
                            "hash-filt-1")));
                IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        JsonPerson(
                            "filt-2@example.com", "first2", "shared",
                            "hash-filt-2")));

                auto assertNoHash = [&](const Json::Value& body,
                                        std::string_view label) {
                    ASSERT_TRUE(body.HasChild("sortedColumnNames", nullptr))
                        << label;
                    for (const auto& col : body["sortedColumnNames"].GetArray()) {
                        EXPECT_NE(col.Get<std::string>(), "password_hash")
                            << "password_hash leaked through "
                               "/api/get_filtered_table_rows ("
                            << label << ")";
                    }
                    for (const auto& row : body["dataTable"].GetArray()) {
                        for (const auto& cell : row.GetArray()) {
                            std::string s = cell.Is<std::string>()
                                ? cell.Get<std::string>()
                                : cell.ToSimpleString();
                            EXPECT_NE(s, "hash-filt-1") << label;
                            EXPECT_NE(s, "hash-filt-2") << label;
                        }
                    }
                };

                // Path 1: no filter pairs — exercises the
                // GetRowsByValuesWithCount(empty lookup) branch.
                {
                    crow::request req;
                    req.method = crow::HTTPMethod::POST;
                    req.url = "/api/get_filtered_table_rows";
                    req.body = MakeRequestBody("people", "id", true, 100, 0);
                    crow::response resp;
                    endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);
                    ASSERT_EQ(resp.code, 200);
                    assertNoHash(Json::Value::FromText(resp.body), "no-filter");
                }

                // Path 2: filter by last_name=shared — exercises the
                // WHERE-clause branch.
                {
                    KeyValueTable filters = { {"last_name", "shared"} };
                    crow::request req;
                    req.method = crow::HTTPMethod::POST;
                    req.url = "/api/get_filtered_table_rows";
                    req.body = MakeRequestBody(
                        "people", "id", true, 100, 0, filters);
                    crow::response resp;
                    endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);
                    ASSERT_EQ(resp.code, 200);
                    assertNoHash(Json::Value::FromText(resp.body), "with-filter");
                }
            });
        }

        TEST(GetFilteredTableRowsTest, EmptyBody) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("EmptyBody", [&](Transaction& transaction) {
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                crow::request req;
                req.method = crow::HTTPMethod::POST;
                req.url = "/api/get_filtered_table_rows";
                req.body = "";
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                });
        }

    } // namespace
}  // namespace Endpoints
