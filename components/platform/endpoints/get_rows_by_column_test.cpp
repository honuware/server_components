#include "get_rows_by_column.h"

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
#include "test/src/util/database_test_helper.h"
#include "test/src/util/test_helper.h"
#include "util/error_codes.h"
#include "util/json_value.h"

namespace Endpoints {
    namespace
    {
        using Json::JsonPair;
        using JsonTestUtil::JsonPerson;

        constexpr std::string_view kByColumnFrag1 = R"JSON(
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

        constexpr std::string_view kByColumnFrag2 = R"JSON(
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

        constexpr std::string_view kByColumnFrag3 = R"JSON(
{
    "sortedColumnNames": [
        "email",
        "first_name",
        "id",
        "last_name",
        "password_hash"
    ],
    "dataTable": [
        ["person5@hotmail.com", "first5", "{id5}", "last5", "hash5"]
    ],
    "totalCount": 5
}
)JSON";

        Json::Value GetTableValues(
            std::string_view formatText,
            int64_t id1,
            int64_t id2,
            int64_t id3,
            int64_t id4,
            int64_t id5) {
            KeyValueTable formatReplace = {
                {"id1", StringFromInt(id1)},
                {"id2", StringFromInt(id2)},
                {"id3", StringFromInt(id3)},
                {"id4", StringFromInt(id4)},
                {"id5", StringFromInt(id5)}
            };
            std::string jsonComp = FormatString(formatText, formatReplace);
			return Json::Value::FromText(jsonComp);
        }

        int64_t IdFromPrimaryKey(const Json::Value& value) {
			return std::stoll(value["id"].Get<std::string>());
        }

        TEST(GetRowsByColumnTest, GetRowsByColumnBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowsByColumnBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                DbSchema::DatabaseInfo databaseInfo = testDatabaseUtil.GetDatabaseInfo();

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

				Json::Value person1 = JsonPerson(
                    "person1@hotmail.com", "first1", "last1", "hash1");
				Json::Value person2 = JsonPerson(
                    "person2@hotmail.com", "first2", "last2", "hash2");
				Json::Value person3 = JsonPerson(
                    "person3@hotmail.com", "first3", "last3", "hash3");
				Json::Value person4 = JsonPerson(
                    "person4@hotmail.com", "first4", "last4", "hash4");
				Json::Value person5 = JsonPerson(
                    "person5@hotmail.com", "first5", "last5", "hash5");

                DatabaseRESTHelper databaseRestHelper(databaseHelper);
                int64_t id1 = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        person1));
                int64_t id2 = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        person2));
                int64_t id3 = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        person3));
                int64_t id4 = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        person4));
                int64_t id5 = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        person5));

                crow::request req;
                crow::response resp;

                req.url = "/api/get_rows_by_column/people/id/1/2/0";
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
					Json::Value::FromText(resp.body),
                    GetTableValues(kByColumnFrag1, id1, id2, id3, id4, id5),
                    DbSchema::kPeopleCreatedAt,
                    DbSchema::kPeopleUpdatedAt,
                    DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));

                resp.clear();

                req.url = "/api/get_rows_by_column/people/id/1/2/1";
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
					Json::Value::FromText(resp.body),
                    GetTableValues(kByColumnFrag2, id1, id2, id3, id4, id5),
                    DbSchema::kPeopleCreatedAt,
                    DbSchema::kPeopleUpdatedAt,
                    DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));

                resp.clear();

                req.url = "/api/get_rows_by_column/people/id/1/2/2";
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
					Json::Value::FromText(resp.body),
                    GetTableValues(kByColumnFrag3, id1, id2, id3, id4, id5),
                    DbSchema::kPeopleCreatedAt,
                    DbSchema::kPeopleUpdatedAt,
                    DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
                });
        }

        // Phase 3.8 of the security review: end-to-end check that
        // /api/get_rows_by_column/people/... never returns the
        // password_hash column to anyone, even an admin.
        TEST(GetRowsByColumnTest, GetRowsByColumnPeopleHidesPasswordHash) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction(
                "GetRowsByColumnPeopleHidesPasswordHash",
                [&](Transaction& transaction) {

                // Insert the redaction BEFORE constructing
                // EndpointTestHelper (which snapshots the redact set
                // into the WebApp at construction time).
                TableHelpers::AdminColumnRedactions redactions(
                    testDatabaseUtil.GetDatabaseHelper());
                redactions.AddAdminColumnRedaction(
                    transaction,
                    DbSchema::kPeopleTable,
                    DbSchema::kPeoplePasswordHash);

                EndpointTestHelper endpointTestHelper(
                    transaction, testDatabaseUtil);
                DbSchema::DatabaseInfo databaseInfo =
                    testDatabaseUtil.GetDatabaseInfo();
                DatabaseHelper databaseHelper =
                    testDatabaseUtil.GetDatabaseHelper();

                endpointTestHelper.AddAllowedTable(
                    transaction, DbSchema::kPeopleTable);

                Json::Value person1 = JsonPerson(
                    "row-col-1@example.com", "first1", "last1", "hash1");
                Json::Value person2 = JsonPerson(
                    "row-col-2@example.com", "first2", "last2", "hash2");

                DatabaseRESTHelper databaseRestHelper(databaseHelper);
                databaseRestHelper.AddTableValueFetchPrimaryKey(
                    transaction, databaseInfo, DbSchema::kPeopleTable, person1);
                databaseRestHelper.AddTableValueFetchPrimaryKey(
                    transaction, databaseInfo, DbSchema::kPeopleTable, person2);

                crow::request req;
                crow::response resp;
                req.url = "/api/get_rows_by_column/people/id/1/100/0";
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);
                ASSERT_TRUE(body.HasChild("sortedColumnNames", nullptr));
                for (const auto& col : body["sortedColumnNames"].GetArray()) {
                    EXPECT_NE(col.Get<std::string>(), "password_hash")
                        << "password_hash leaked through "
                           "/api/get_rows_by_column/people";
                }
                // Cell-level scan: no row should contain "hash1" or "hash2".
                for (const auto& row : body["dataTable"].GetArray()) {
                    for (const auto& cell : row.GetArray()) {
                        std::string s = cell.Is<std::string>()
                            ? cell.Get<std::string>()
                            : cell.ToSimpleString();
                        EXPECT_NE(s, "hash1");
                        EXPECT_NE(s, "hash2");
                    }
                }
            });
        }

        TEST(GetRowsByColumnTest, GetRowsByColumnNotAllowed) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowsByColumnNotAllowed", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                DbSchema::DatabaseInfo databaseInfo = testDatabaseUtil.GetDatabaseInfo();

				Json::Value person1 = JsonPerson(
                    "person1@hotmail.com", "first1", "last1", "hash1");
				Json::Value person2 = JsonPerson(
                    "person2@hotmail.com", "first2", "last2", "hash2");

				Json::JsonArray valueArray;
				valueArray.emplace_back(std::move(person1));
				valueArray.emplace_back(std::move(person2));
				Json::Value peopleList(std::move(valueArray));

                crow::request req;
                req.url = "/api/get_rows_by_column/people/id/1/2/0";
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(), "Table(people) is not an allowed table.");
                });
        }

    } // namespace 
}  // namespace Endpoints
