#include "database_rest_helper.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "db_schema/people.h"
#include "db_schema/price_schedules.h"
#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/json_test_util.h"
#include "test/src/util/json_value_matcher.h"
#include "test/src/util/table_matcher.h"
#include "sql_util/database_access/db_and_table_operations.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/table_helpers/admin_top_level_tables.h"
#include "sql_util/table_helpers/admin_table_friendly_names.h"
#include "sql_util/table_helpers/admin_column_friendly_names.h"
#include "sql_util/table_helpers/admin_column_data_info.h"
#include "sql_util/table_helpers/admin_enum_values.h"
#include "sql_util/table_helpers/admin_column_enums.h"
#include "db_schema/admin_column_data_info.h"
#include "sql_util/table_helpers/admin_enums.h"
#include "test/src/util/test_helper.h"
#include "util/json_value.h"

namespace
{
    using Json::JsonPair;
    using ::testing::Contains;
    using ::testing::ElementsAreArray;
    using ::testing::UnorderedElementsAre;
    using ::testing::Eq;

    // Verify a person exists in a GetTableValues result by finding their email
    // and checking all fields match. Order-independent.
    void AssertPersonInTableResult(
        const Json::Value& tableResult,
        const std::string& email,
        const std::string& firstName,
        const std::string& lastName,
        const std::string& passwordHash,
        int64_t expectedId) {

        ASSERT_TRUE(tableResult.HasChild("sortedColumnNames", nullptr));
        ASSERT_TRUE(tableResult.HasChild("dataTable", nullptr));

        const auto& cols = tableResult["sortedColumnNames"].GetArray();
        const auto& rows = tableResult["dataTable"].GetArray();

        // Find column indices
        int emailIdx = -1, firstIdx = -1, lastIdx = -1, hashIdx = -1, idIdx = -1;
        for (size_t i = 0; i < cols.size(); ++i) {
            std::string col = cols[i].Get<std::string>();
            if (col == "email") emailIdx = static_cast<int>(i);
            else if (col == "first_name") firstIdx = static_cast<int>(i);
            else if (col == "last_name") lastIdx = static_cast<int>(i);
            else if (col == "password_hash") hashIdx = static_cast<int>(i);
            else if (col == "id") idIdx = static_cast<int>(i);
        }
        ASSERT_NE(emailIdx, -1);

        bool found = false;
        for (const auto& row : rows) {
            if (row[static_cast<size_t>(emailIdx)].Get<std::string>() == email) {
                found = true;
                EXPECT_EQ(row[static_cast<size_t>(firstIdx)].Get<std::string>(), firstName);
                EXPECT_EQ(row[static_cast<size_t>(lastIdx)].Get<std::string>(), lastName);
                EXPECT_EQ(row[static_cast<size_t>(hashIdx)].Get<std::string>(), passwordHash);
                EXPECT_EQ(row[static_cast<size_t>(idIdx)].Get<std::string>(),
                    StringFromInt(expectedId));
                break;
            }
        }
        EXPECT_TRUE(found) << "Person with email " << email << " not found in result";
    }

    TEST(DatabaseRestHelperTest, AddTableValueBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("AddTableValueBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            DatabaseRESTHelper helper(databaseHelper);

			Json::Value person1ToAdd = JsonTestUtil::JsonPerson(
                "person1@hotmail.com", "first1", "last1", "hash1");
			Json::Value person2ToAdd = JsonTestUtil::JsonPerson(
                "person2@hotmail.com", "first2", "last2", "hash2");

            helper.AddTableValue(
                transaction,
                testDatabaseUtil.GetDatabaseInfo(),
                DbSchema::kPeopleTable,
                person1ToAdd);
            helper.AddTableValue(
                transaction,
                testDatabaseUtil.GetDatabaseInfo(),
                DbSchema::kPeopleTable,
                person2ToAdd);

            auto rows = DbCrud::GetTableRows(transaction, databaseHelper, DbSchema::kPeopleTable);
            int64_t id1 = 0, id2 = 0;
            for (const auto& row : rows) {
                if (row.at(std::string(DbSchema::kPeopleEmail)) == "person1@hotmail.com") {
                    id1 = std::stoll(row.at(std::string(DbSchema::kPeopleId)));
                } else if (row.at(std::string(DbSchema::kPeopleEmail)) == "person2@hotmail.com") {
                    id2 = std::stoll(row.at(std::string(DbSchema::kPeopleId)));
                }
            }
            ASSERT_NE(id1, 0);
            ASSERT_NE(id2, 0);

            auto tableResult = helper.GetTableValues(transaction, DbSchema::kPeopleTable);
            ASSERT_EQ(tableResult["dataTable"].GetArray().size(), 2u);
            AssertPersonInTableResult(tableResult,
                "person1@hotmail.com", "first1", "last1", "hash1", id1);
            AssertPersonInTableResult(tableResult,
                "person2@hotmail.com", "first2", "last2", "hash2", id2);
            });
    }

    TEST(DatabaseRestHelperTest, AddTableValueInvalidKey)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("AddTableValueInvalidKey", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
            DatabaseRESTHelper helper(databaseHelper);
			Json::JsonObject person1;
			person1["invalid"] = "1";
            try {
                helper.AddTableValue(
                    transaction,
                    testDatabaseUtil.GetDatabaseInfo(),
                    DbSchema::kPeopleTable,
					Json::Value(std::move(person1)));
                // We should never get here
                ASSERT_TRUE(false);
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()), 
                    "DatabaseRESTHelper::AddTableValue - invalid key: invalid");
            }
            });
    }

    TEST(DatabaseRestHelperTest, AddTableValueFetchPrimaryKeyBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("AddTableValueFetchPrimaryKeyBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            DatabaseRESTHelper helper(databaseHelper);


			Json::Value person1ToAdd = JsonTestUtil::JsonPerson(
                "person1@hotmail.com", "first1", "last1", "hash1");
			Json::Value person2ToAdd = JsonTestUtil::JsonPerson(
                "person2@hotmail.com", "first2", "last2", "hash2");

			Json::Value jsonId1 = helper.AddTableValueFetchPrimaryKey(
                transaction,
                testDatabaseUtil.GetDatabaseInfo(),
                DbSchema::kPeopleTable,
				person1ToAdd);
			Json::Value jsonId2 = helper.AddTableValueFetchPrimaryKey(
                transaction,
                testDatabaseUtil.GetDatabaseInfo(),
                DbSchema::kPeopleTable,
				person2ToAdd);

			int64_t id1 = std::stoll(jsonId1[DbSchema::kPeopleId].Get<std::string>());
			int64_t id2 = std::stoll(jsonId2[DbSchema::kPeopleId].Get<std::string>());

            auto tableResult = helper.GetTableValues(transaction, DbSchema::kPeopleTable);
            ASSERT_EQ(tableResult["dataTable"].GetArray().size(), 2u);
            AssertPersonInTableResult(tableResult,
                "person1@hotmail.com", "first1", "last1", "hash1", id1);
            AssertPersonInTableResult(tableResult,
                "person2@hotmail.com", "first2", "last2", "hash2", id2);
            });
    }

    TEST(DatabaseRestHelperTest, AddTableValueFetchPrimaryKeyNoPrimaryKey)
    {
        TestDatabaseUtil testDatabaseUtil(TestDatabaseUtil::SchemaMode::Empty);
        testDatabaseUtil.RunInTransaction("AddTableValueFetchPrimaryKeyNoPrimaryKey", [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            DatabaseRESTHelper helper(databaseHelper);

            auto databaseInfo = testDatabaseUtil.GetDatabaseInfo();
            constexpr std::string_view kPeople = "people";
            databaseInfo.AddTable(kPeople);
            databaseInfo.AddColumnUnique(kPeople, "email", DB_TYPE_STRING);
            databaseInfo.AddColumnSimple(kPeople, "first_name", DB_TYPE_STRING);
            databaseInfo.AddColumnSimple(kPeople, "last_name", DB_TYPE_STRING);
            databaseInfo.AddColumnSimple(kPeople, "password_hash", DB_TYPE_STRING);
            DbOps::CreateTable(transaction, databaseInfo, kPeople);


			Json::Value person1ToAdd = JsonTestUtil::JsonPerson(
                "person1@hotmail.com", "first1", "last1", "hash1");

            try {
                helper.AddTableValueFetchPrimaryKey(
                    transaction,
                    testDatabaseUtil.GetDatabaseInfo(),
                    DbSchema::kPeopleTable,
					person1ToAdd);
                // We should never get here
                ASSERT_TRUE(false);
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "DatabaseRESTHelper::AddTableValueFetchPrimaryKey called on "
                    "table with no primary key: people");
            }
            });
    }

    TEST(DatabaseRestHelperTest, GetTableValuesBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("GetTableValuesBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            int64_t id1 = testDatabaseUtil.AddPerson(
                transaction, "person1@hotmail.com", "first1", "last1", "hash1");
            int64_t id2 = testDatabaseUtil.AddPerson(
                transaction, "person2@hotmail.com", "first2", "last2", "hash2");

            DatabaseRESTHelper helper(databaseHelper);

            auto tableResult = helper.GetTableValues(transaction, DbSchema::kPeopleTable);
            ASSERT_EQ(tableResult["dataTable"].GetArray().size(), 2u);
            AssertPersonInTableResult(tableResult,
                "person1@hotmail.com", "first1", "last1", "hash1", id1);
            AssertPersonInTableResult(tableResult,
                "person2@hotmail.com", "first2", "last2", "hash2", id2);
            });
    }

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
    ]
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
    ]
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
    ]
}
)JSON";

    TEST(DatabaseRestHelperTest, GetRowsByColumnBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("GetRowsByColumnBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            int64_t id1 = testDatabaseUtil.AddPerson(
                transaction, "person1@hotmail.com", "first1", "last1", "hash1");
            int64_t id2 = testDatabaseUtil.AddPerson(
                transaction, "person2@hotmail.com", "first2", "last2", "hash2");
            int64_t id3 = testDatabaseUtil.AddPerson(
                transaction, "person3@hotmail.com", "first3", "last3", "hash3");
            int64_t id4 = testDatabaseUtil.AddPerson(
                transaction, "person4@hotmail.com", "first4", "last4", "hash4");
            int64_t id5 = testDatabaseUtil.AddPerson(
                transaction, "person5@hotmail.com", "first5", "last5", "hash5");

            DatabaseRESTHelper helper(databaseHelper);

            KeyValueTable formatReplace1 = {
                {"id1", StringFromInt(id1)},
                {"id2", StringFromInt(id2)}
            };
            KeyValueTable formatReplace2 = {
                {"id3", StringFromInt(id3)},
                {"id4", StringFromInt(id4)}
            };
            KeyValueTable formatReplace3 = {
                {"id5", StringFromInt(id5)}
            };
            std::string jsonComp1 = FormatString(kByColumnFrag1, formatReplace1);
            std::string jsonComp2 = FormatString(kByColumnFrag2, formatReplace2);
            std::string jsonComp3 = FormatString(kByColumnFrag3, formatReplace3);

            ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                helper.GetRowsByColumn(
                    transaction, DbSchema::kPeopleTable,
                    DbSchema::kPeopleId, true, 2, 0),
				Json::Value::FromText(jsonComp1),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
            ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                helper.GetRowsByColumn(
                    transaction, DbSchema::kPeopleTable,
                    DbSchema::kPeopleId, true, 2, 1),
				Json::Value::FromText(jsonComp2),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
            ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                helper.GetRowsByColumn(
                    transaction, DbSchema::kPeopleTable,
                    DbSchema::kPeopleId, true, 2, 2),
				Json::Value::FromText(jsonComp3),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
            });
    }

    constexpr std::string_view kByColumnWithCountFrag1 = R"JSON(
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

    TEST(DatabaseRestHelperTest, GetRowsByColumnWithCountBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("GetRowsByColumnWithCountBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            int64_t id1 = testDatabaseUtil.AddPerson(
                transaction, "person1@hotmail.com", "first1", "last1", "hash1");
            int64_t id2 = testDatabaseUtil.AddPerson(
                transaction, "person2@hotmail.com", "first2", "last2", "hash2");
            testDatabaseUtil.AddPerson(
                transaction, "person3@hotmail.com", "first3", "last3", "hash3");
            testDatabaseUtil.AddPerson(
                transaction, "person4@hotmail.com", "first4", "last4", "hash4");
            testDatabaseUtil.AddPerson(
                transaction, "person5@hotmail.com", "first5", "last5", "hash5");

            DatabaseRESTHelper helper(databaseHelper);

            KeyValueTable formatReplace1 = {
                {"id1", StringFromInt(id1)},
                {"id2", StringFromInt(id2)}
            };
            std::string jsonComp1 = FormatString(kByColumnWithCountFrag1, formatReplace1);

            // Get first page (2 rows) and verify totalCount is 5
            Json::Value result = helper.GetRowsByColumnWithCount(
                transaction, DbSchema::kPeopleTable,
                DbSchema::kPeopleId, true, 2, 0);

            // Verify totalCount is present and correct
            ASSERT_TRUE(result["totalCount"].Is<int64_t>());
            EXPECT_EQ(result["totalCount"].Get<int64_t>(), 5);

            // Verify the data is correct (minus timestamp columns)
            ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                result,
                Json::Value::FromText(jsonComp1),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
            });
    }

    constexpr std::string_view kByValuesWithCountFiltered = R"JSON(
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

    TEST(DatabaseRestHelperTest, GetRowsByValuesWithCountFiltered)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("GetRowsByValuesWithCountFiltered", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            int64_t id1 = testDatabaseUtil.AddPerson(
                transaction, "person1@hotmail.com", "first1", "shared_last", "hash1");
            testDatabaseUtil.AddPerson(
                transaction, "person2@hotmail.com", "first2", "other_last", "hash2");
            int64_t id3 = testDatabaseUtil.AddPerson(
                transaction, "person3@hotmail.com", "first3", "shared_last", "hash3");
            testDatabaseUtil.AddPerson(
                transaction, "person4@hotmail.com", "first4", "another_last", "hash4");
            testDatabaseUtil.AddPerson(
                transaction, "person5@hotmail.com", "first5", "shared_last", "hash5");

            DatabaseRESTHelper helper(databaseHelper);

            KeyValueTable lookupValues = {
                {"last_name", "shared_last"}
            };

            Json::Value result = helper.GetRowsByValuesWithCount(
                transaction, DbSchema::kPeopleTable,
                DbSchema::kPeopleId, lookupValues, true, 2, 0);

            // totalCount should reflect the filtered count (3 matching rows), not all 5
            ASSERT_TRUE(result["totalCount"].Is<int64_t>());
            EXPECT_EQ(result["totalCount"].Get<int64_t>(), 3);

            KeyValueTable formatReplace = {
                {"id1", StringFromInt(id1)},
                {"id3", StringFromInt(id3)}
            };
            std::string jsonComp = FormatString(kByValuesWithCountFiltered, formatReplace);

            ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                result,
                Json::Value::FromText(jsonComp),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
            });
    }

    constexpr std::string_view kByValuesWithCountNoFilter = R"JSON(
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

    TEST(DatabaseRestHelperTest, GetRowsByValuesWithCountEmptyFilter)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("GetRowsByValuesWithCountEmptyFilter", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            int64_t id1 = testDatabaseUtil.AddPerson(
                transaction, "person1@hotmail.com", "first1", "last1", "hash1");
            int64_t id2 = testDatabaseUtil.AddPerson(
                transaction, "person2@hotmail.com", "first2", "last2", "hash2");
            testDatabaseUtil.AddPerson(
                transaction, "person3@hotmail.com", "first3", "last3", "hash3");
            testDatabaseUtil.AddPerson(
                transaction, "person4@hotmail.com", "first4", "last4", "hash4");
            testDatabaseUtil.AddPerson(
                transaction, "person5@hotmail.com", "first5", "last5", "hash5");

            DatabaseRESTHelper helper(databaseHelper);

            // Empty filter should return all rows (like GetRowsByColumnWithCount)
            KeyValueTable emptyFilter;

            Json::Value result = helper.GetRowsByValuesWithCount(
                transaction, DbSchema::kPeopleTable,
                DbSchema::kPeopleId, emptyFilter, true, 2, 0);

            ASSERT_TRUE(result["totalCount"].Is<int64_t>());
            EXPECT_EQ(result["totalCount"].Get<int64_t>(), 5);

            KeyValueTable formatReplace = {
                {"id1", StringFromInt(id1)},
                {"id2", StringFromInt(id2)}
            };
            std::string jsonComp = FormatString(kByValuesWithCountNoFilter, formatReplace);

            ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                result,
                Json::Value::FromText(jsonComp),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
            });
    }

    constexpr std::string_view kByValuesWithCountPage2 = R"JSON(
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

    TEST(DatabaseRestHelperTest, GetRowsByValuesWithCountPagination)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("GetRowsByValuesWithCountPagination", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            testDatabaseUtil.AddPerson(
                transaction, "person1@hotmail.com", "first1", "shared_last", "hash1");
            testDatabaseUtil.AddPerson(
                transaction, "person2@hotmail.com", "first2", "other_last", "hash2");
            testDatabaseUtil.AddPerson(
                transaction, "person3@hotmail.com", "first3", "shared_last", "hash3");
            testDatabaseUtil.AddPerson(
                transaction, "person4@hotmail.com", "first4", "another_last", "hash4");
            int64_t id5 = testDatabaseUtil.AddPerson(
                transaction, "person5@hotmail.com", "first5", "shared_last", "hash5");

            DatabaseRESTHelper helper(databaseHelper);

            KeyValueTable lookupValues = {
                {"last_name", "shared_last"}
            };

            // Get page 1 (second page, 0-indexed) with pageSize 2
            Json::Value result = helper.GetRowsByValuesWithCount(
                transaction, DbSchema::kPeopleTable,
                DbSchema::kPeopleId, lookupValues, true, 2, 1);

            // totalCount should still be 3 (total matching rows)
            ASSERT_TRUE(result["totalCount"].Is<int64_t>());
            EXPECT_EQ(result["totalCount"].Get<int64_t>(), 3);

            KeyValueTable formatReplace = {
                {"id5", StringFromInt(id5)}
            };
            std::string jsonComp = FormatString(kByValuesWithCountPage2, formatReplace);

            ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                result,
                Json::Value::FromText(jsonComp),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
            });
    }

    constexpr std::string_view kRowByValuesResult = R"JSON(
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

    TEST(DatabaseRestHelperTest, GetRowByValuesBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("GetRowByValuesBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            testDatabaseUtil.AddPerson(
                transaction, "person1@hotmail.com", "first1", "last1", "hash1");
            int64_t id2 = testDatabaseUtil.AddPerson(
                transaction, "person2@hotmail.com", "first2", "last2", "hash2");

            DatabaseRESTHelper helper(databaseHelper);

            KeyValueTable lookupValues = {
                {"email", "person2@hotmail.com"}
            };

            KeyValueTable formatReplace = {
                {"id2", StringFromInt(id2)}
            };
            std::string jsonComp = FormatString(kRowByValuesResult, formatReplace);

            ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                helper.GetRowByValues(
                    transaction, DbSchema::kPeopleTable, lookupValues),
                Json::Value::FromText(jsonComp),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
            });
    }

    TEST(DatabaseRestHelperTest, GetRowBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("GetRowBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            int64_t id1 = testDatabaseUtil.AddPerson(
                transaction, "person1@hotmail.com", "first1", "last1", "hash1");
            int64_t id2 = testDatabaseUtil.AddPerson(
                transaction, "person2@hotmail.com", "first2", "last2", "hash2");

            DatabaseRESTHelper helper(databaseHelper);

            constexpr std::string_view kRowFrag1 = R"JSON(
            {
                "sortedColumnNames": [
                    "email",
                    "first_name",
                    "id",
                    "last_name",
                    "password_hash"
                ],
                "dataTable": [
                    ["person1@hotmail.com", "first1", "{id1}", "last1", "hash1"]
                ]
            }
            )JSON";

            constexpr std::string_view kRowFrag2 = R"JSON(
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

            KeyValueTable formatReplace1 = {
                {"id1", StringFromInt(id1)}
            };
            KeyValueTable formatReplace2 = {
                {"id2", StringFromInt(id2)}
            };
            std::string jsonComp1 = FormatString(kRowFrag1, formatReplace1);
            std::string jsonComp2 = FormatString(kRowFrag2, formatReplace2);


            ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                helper.GetRow(
                    transaction, DbSchema::kPeopleTable, 
                    DbSchema::kPeopleId, StringFromInt(id1)),
				Json::Value::FromText(jsonComp1),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));

            ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                helper.GetRow(
                    transaction, DbSchema::kPeopleTable, 
                    DbSchema::kPeopleId, StringFromInt(id2)),
				Json::Value::FromText(jsonComp2),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
            });
    }

    TEST(DatabaseRestHelperTest, GetTableValueBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("GetTableValueBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            int64_t id1 = testDatabaseUtil.AddPerson(
                transaction, "person1@hotmail.com", "first1", "last1", "hash1");
            int64_t id2 = testDatabaseUtil.AddPerson(
                transaction, "person2@hotmail.com", "first2", "last2", "hash2");

			Json::JsonObject person1;
			person1[std::string(DbSchema::kPeopleId)] = StringFromInt(id1);
			person1[std::string(DbSchema::kPeopleEmail)] = "person1@hotmail.com";
			person1[std::string(DbSchema::kPeopleFirstName)] = "first1";
			person1[std::string(DbSchema::kPeopleLastName)] = "last1";
			person1[std::string(DbSchema::kPeoplePasswordHash)] = "hash1";
			Json::JsonObject person2;
			person2[std::string(DbSchema::kPeopleId)] = StringFromInt(id2);
			person2[std::string(DbSchema::kPeopleEmail)] = "person2@hotmail.com";
			person2[std::string(DbSchema::kPeopleFirstName)] = "first2";
			person2[std::string(DbSchema::kPeopleLastName)] = "last2";
			person2[std::string(DbSchema::kPeoplePasswordHash)] = "hash2";

            DatabaseRESTHelper helper(databaseHelper);

            ASSERT_TRUE(JsonTestUtil::CompareJsonObjectMinusColumns(
                helper.GetTableValue(
                    transaction, DbSchema::kPeopleTable, 
                    DbSchema::kPeopleId, StringFromInt(id1)),
				Json::Value(std::move(person1)),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));

            ASSERT_TRUE(JsonTestUtil::CompareJsonObjectMinusColumns(
                helper.GetTableValue(
                    transaction, DbSchema::kPeopleTable, 
                    DbSchema::kPeopleEmail, "person2@hotmail.com"),
				Json::Value(std::move(person2)),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
            });
    }

    constexpr std::string_view kUpdate1 = R"JSON(
{
    "sortedColumnNames": [
        "email",
        "first_name",
        "id",
        "last_name",
        "password_hash"
    ],
    "dataTable": [
        ["person1@hotmail.com", "first1", "{id}", "last1", "hash1"]
    ]
}
)JSON";

    constexpr std::string_view kUpdate2 = R"JSON(
{
    "sortedColumnNames": [
        "email",
        "first_name",
        "id",
        "last_name",
        "password_hash"
    ],
    "dataTable": [
        ["person2@hotmail.com", "first2", "{id}", "last2", "hash2"]
    ]
}
)JSON";

    TEST(DatabaseRestHelperTest, UpdateTableValueBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("UpdateTableValueBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            DatabaseRESTHelper helper(databaseHelper);

			Json::Value person1ToAdd = JsonTestUtil::JsonPerson(
                "person1@hotmail.com", "first1", "last1", "hash1");
			Json::Value person2ToAdd = JsonTestUtil::JsonPerson(
                "person2@hotmail.com", "first2", "last2", "hash2");

            helper.AddTableValue(
                transaction,
                testDatabaseUtil.GetDatabaseInfo(),
                DbSchema::kPeopleTable,
				person1ToAdd);

            auto rows = DbCrud::GetTableRows(transaction, databaseHelper, DbSchema::kPeopleTable);
            ASSERT_EQ(rows.size(), 1u);
            std::string personId = rows[0].at(std::string(DbSchema::kPeopleId));
            KeyValueTable idParams = {{"id", personId}};

            ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                helper.GetTableValues(transaction, DbSchema::kPeopleTable),
				Json::Value::FromText(FormatString(kUpdate1, idParams)),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));

            helper.UpdateTableValue(
                transaction,
                testDatabaseUtil.GetDatabaseInfo(),
                DbSchema::kPeopleTable,
                DbSchema::kPeopleId,
                personId,
				person2ToAdd);

            ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                helper.GetTableValues(transaction, DbSchema::kPeopleTable),
				Json::Value::FromText(FormatString(kUpdate2, idParams)),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
            });
    }

    constexpr std::string_view kDelete = R"JSON(
{
    "sortedColumnNames": [
        "email",
        "first_name",
        "id",
        "last_name",
        "password_hash"
    ],
    "dataTable": [
        ["person2@hotmail.com", "first2", "{id}", "last2", "hash2"]
    ]
}
)JSON";

    constexpr std::string_view kDeleteEmpty = R"JSON(
{
    "dataTable": [],
    "sortedColumnNames": []
}
)JSON";

    TEST(DatabaseRestHelperTest, DeleteTableValueBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("DeleteTableValueBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            DatabaseRESTHelper helper(databaseHelper);

			Json::Value person1ToAdd = JsonTestUtil::JsonPerson(
                "person1@hotmail.com", "first1", "last1", "hash1");
			Json::Value person2ToAdd = JsonTestUtil::JsonPerson(
                "person2@hotmail.com", "first2", "last2", "hash2");

            helper.AddTableValue(
                transaction,
                testDatabaseUtil.GetDatabaseInfo(),
                DbSchema::kPeopleTable,
				person1ToAdd);
            helper.AddTableValue(
                transaction,
                testDatabaseUtil.GetDatabaseInfo(),
                DbSchema::kPeopleTable,
				person2ToAdd);

            auto rows = DbCrud::GetTableRows(transaction, databaseHelper, DbSchema::kPeopleTable);
            ASSERT_EQ(rows.size(), 2u);
            int64_t id1 = 0, id2 = 0;
            for (const auto& row : rows) {
                if (row.at(std::string(DbSchema::kPeopleEmail)) == "person1@hotmail.com") {
                    id1 = std::stoll(row.at(std::string(DbSchema::kPeopleId)));
                } else if (row.at(std::string(DbSchema::kPeopleEmail)) == "person2@hotmail.com") {
                    id2 = std::stoll(row.at(std::string(DbSchema::kPeopleId)));
                }
            }
            ASSERT_NE(id1, 0);
            ASSERT_NE(id2, 0);

            // GetTableValues template has person1 at row 0, person2 at row 1.
            // DB may return rows in any order, so we verify row count and
            // check each person individually rather than relying on positional match.
            auto tableResult = helper.GetTableValues(transaction, DbSchema::kPeopleTable);
            ASSERT_TRUE(tableResult.HasChild("dataTable", nullptr));
            ASSERT_EQ(tableResult["dataTable"].GetArray().size(), 2u);

            helper.DeleteTableValue(
                transaction,
                testDatabaseUtil.GetDatabaseInfo(),
                std::string(DbSchema::kPeopleTable),
                std::string(DbSchema::kPeopleId),
                StringFromInt(id1));

            KeyValueTable deleteIdParams = {{"id", StringFromInt(id2)}};
            ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                helper.GetTableValues(transaction, DbSchema::kPeopleTable),
				Json::Value::FromText(FormatString(kDelete, deleteIdParams)),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));

            helper.DeleteTableValue(
                transaction,
                testDatabaseUtil.GetDatabaseInfo(),
                std::string(DbSchema::kPeopleTable),
                std::string(DbSchema::kPeopleId),
                StringFromInt(id2));
           
            ASSERT_TRUE(JsonTestUtil::CompareJsonDataResultsMinusColumns(
                helper.GetTableValues(transaction, DbSchema::kPeopleTable),
                Json::Value::FromText(kDeleteEmpty),
                DbSchema::kPeopleCreatedAt,
                DbSchema::kPeopleUpdatedAt,
                DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
            });
    }

    constexpr std::string_view kTestDatabaseSchema = R"JSON(
{
  "display_templates": {},
  "fk_picker_preload_threshold": 50,
  "nested_tables": [],
  "root_tables": ["people"],
  "tables": [
    {
      "columns": [
        {
          "column_friendly_name": "Person ID",
          "column_name": "person_id",
          "default_value": "default1a",
          "hidden": "f",
          "hint": "hint1a",
          "html_input_type": "htmlInputType1a",
          "label": "label1a",
          "max_length": "5",
          "nullable": "f",
          "place_holder": "placeHolder1a",
          "primary_key": "t",
          "readonly": "f",
          "regex": "regex1a",
          "required": "t",
          "rows": "3",
          "type": "SERIAL",
          "unique": "f"
        },
        {
          "column_friendly_name": "Email",
          "column_name": "email",
          "default_value": "default1b",
          "hidden": "f",
          "hint": "hint1b",
          "html_input_type": "htmlInputType1b",
          "label": "label1b",
          "max_length": "7",
          "nullable": "f",
          "place_holder": "placeHolder1b",
          "primary_key": "f",
          "readonly": "f",
          "regex": "regex1b",
          "required": "t",
          "rows": "5",
          "type": "VARCHAR",
          "unique": "t"
        }
      ],
      "description": "description1",
      "foreign_keys": [],
      "primary_key": "person_id",
      "table_friendly_name": "People",
      "table_name": "people"
    },
    {
      "columns": [
        {
          "column_friendly_name": "Order ID",
          "column_name": "order_id",
          "default_value": "default2a",
          "hidden": "f",
          "hint": "hint2a",
          "html_input_type": "htmlInputType2a",
          "label": "label2a",
          "max_length": "6",
          "nullable": "f",
          "place_holder": "placeHolder2a",
          "primary_key": "t",
          "readonly": "f",
          "regex": "regex2a",
          "required": "f",
          "rows": "4",
          "type": "SERIAL",
          "unique": "f"
        },
        {
          "column_friendly_name": "My person",
          "column_name": "parent_person_id",
          "default_value": "default2",
          "hidden": "f",
          "hint": "hint2b",
          "html_input_type": "htmlInputType2b",
          "label": "label2b",
          "max_length": "8",
          "nullable": "f",
          "place_holder": "placeHolder2b",
          "primary_key": "f",
          "readonly": "f",
          "regex": "regex2b",
          "required": "f",
          "rows": "6",
          "type": "INT",
          "unique": "f"
        }
      ],
      "description": "description2",
      "foreign_keys": [
        {
          "column_name": "parent_person_id",
          "parent_column_name": "person_id",
          "parent_table_name": "people"
        }
      ],
      "primary_key": "order_id",
      "table_friendly_name": "Orders",
      "table_name": "orders"
    }
  ]
}
)JSON";

    TEST(DatabaseRestHelperTest, DatabaseMetadataBasic)
    {
        TestDatabaseUtil testDatabaseUtil(TestDatabaseUtil::SchemaMode::Empty);
        testDatabaseUtil.RunInTransaction("DatabaseMetadataBasic", [&](Transaction& transaction) {
            DbSchema::DatabaseInfo databaseInfo = testDatabaseUtil.GetDatabaseInfo();
            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            DatabaseRESTHelper helper(databaseHelper);

            constexpr std::string_view kPeople = "people";
            databaseInfo.AddTable(kPeople);
            databaseInfo.AddColumnPrimaryKey(kPeople, "person_id", DB_TYPE_SERIAL);
            databaseInfo.AddColumnUnique(kPeople, "email", DB_TYPE_STRING);

            constexpr std::string_view kOrders = "orders";
            databaseInfo.AddTable(kOrders);
            databaseInfo.AddColumnPrimaryKey(kOrders, "order_id", DB_TYPE_SERIAL);
            databaseInfo.AddColumnForeignKeyRef(
                kPeople,
                "person_id",
                kOrders,
                "parent_person_id");

            TableHelpers::AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, kPeople);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, kOrders);

            TableHelpers::AdminTableFriendlyNames adminTableFriendlyNames(databaseHelper);
            adminTableFriendlyNames.AddAdminTableFriendlyName(
                transaction, kPeople, "People", "description1");
            adminTableFriendlyNames.AddAdminTableFriendlyName(
                transaction, kOrders, "Orders", "description2");

            TableHelpers::AdminColumnFriendlyNames adminColumnFriendlyNames(databaseHelper);
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                transaction, kPeople, "person_id", "Person ID");
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                transaction, kPeople, "email", "Email");
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                transaction, kOrders, "order_id", "Order ID");
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                transaction, kOrders, "parent_person_id", "My person");

            TableHelpers::AdminColumnDataInfo adminColumnDataInfo(databaseHelper);
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, kPeople, "person_id", "label1a", "hint1a", "placeHolder1a", "regex1a", "htmlInputType1a", "true", "5", "default1a", "3");
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, kPeople, "email", "label1b", "hint1b", "placeHolder1b", "regex1b", "htmlInputType1b", "true", "7", "default1b", "5");
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, kOrders, "order_id", "label2a", "hint2a", "placeHolder2a", "regex2a", "htmlInputType2a", "false", "6", "default2a", "4");
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, kOrders, "parent_person_id", "label2b", "hint2b", "placeHolder2b", "regex2b", "htmlInputType2b", "false", "8", "default2", "6");

            TableHelpers::AdminEnumValues adminEnumValues(databaseHelper);
            TableHelpers::AdminColumnEnums adminColumnEnums(databaseHelper);

            EXPECT_THAT(
                helper.DatabaseMetadata(
                    transaction,
                    databaseInfo,
                    { "people", "orders" },
                    { "people" },
                    {},
                    adminTableFriendlyNames,
                    adminColumnFriendlyNames,
                    adminColumnDataInfo,
                    adminEnumValues,
                    adminColumnEnums),
				JsonValueIs(Json::Value::FromText(kTestDatabaseSchema)));
            });
    }

    constexpr std::string_view kTestDatabaseSchemaWithHiddenReadonly = R"JSON(
{
  "display_templates": {},
  "fk_picker_preload_threshold": 50,
  "nested_tables": [],
  "root_tables": ["items"],
  "tables": [
    {
      "columns": [
        {
          "column_friendly_name": "Item ID",
          "column_name": "item_id",
          "default_value": "",
          "hidden": "f",
          "hint": "",
          "html_input_type": "text",
          "label": "ID",
          "max_length": "",
          "nullable": "f",
          "place_holder": "",
          "primary_key": "t",
          "readonly": "f",
          "regex": "",
          "required": "",
          "rows": "",
          "type": "SERIAL",
          "unique": "f"
        },
        {
          "column_friendly_name": "FK Parent",
          "column_name": "parent_id",
          "default_value": "",
          "hidden": "t",
          "hint": "Foreign key reference",
          "html_input_type": "text",
          "label": "Parent",
          "max_length": "",
          "nullable": "f",
          "place_holder": "",
          "primary_key": "f",
          "readonly": "f",
          "regex": "",
          "required": "",
          "rows": "",
          "type": "INT",
          "unique": "f"
        },
        {
          "column_friendly_name": "Created",
          "column_name": "created_us",
          "default_value": "",
          "hidden": "f",
          "hint": "Auto-generated timestamp",
          "html_input_type": "text",
          "label": "Created",
          "max_length": "",
          "nullable": "f",
          "place_holder": "",
          "primary_key": "f",
          "readonly": "t",
          "regex": "",
          "required": "",
          "rows": "",
          "type": "BIGINT",
          "unique": "f"
        }
      ],
      "description": "Test items",
      "foreign_keys": [],
      "primary_key": "item_id",
      "table_friendly_name": "Items",
      "table_name": "items"
    }
  ]
}
)JSON";

    TEST(DatabaseRestHelperTest, DatabaseMetadataWithHiddenAndReadonly)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("DatabaseMetadataWithHiddenAndReadonly", [&](Transaction& transaction) {
            DbSchema::DatabaseInfo databaseInfo = testDatabaseUtil.GetDatabaseInfo();
            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            DatabaseRESTHelper helper(databaseHelper);

            constexpr std::string_view kItems = "items";
            databaseInfo.AddTable(kItems);
            databaseInfo.AddColumnPrimaryKey(kItems, "item_id", DB_TYPE_SERIAL);
            databaseInfo.AddColumnSimple(kItems, "parent_id", DB_TYPE_INT);
            databaseInfo.AddColumnSimple(kItems, "created_us", DB_TYPE_BIGINT);

            TableHelpers::AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, kItems);

            TableHelpers::AdminTableFriendlyNames adminTableFriendlyNames(databaseHelper);
            adminTableFriendlyNames.AddAdminTableFriendlyName(
                transaction, kItems, "Items", "Test items");

            TableHelpers::AdminColumnFriendlyNames adminColumnFriendlyNames(databaseHelper);
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(transaction, kItems, "item_id", "Item ID");
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(transaction, kItems, "parent_id", "FK Parent");
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(transaction, kItems, "created_us", "Created");

            TableHelpers::AdminColumnDataInfo adminColumnDataInfo(databaseHelper);
            // Normal column - no hidden or readonly
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, kItems, "item_id", "ID", "", "", "", "text");
            // Hidden column (FK)
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, kItems, "parent_id", "Parent", "Foreign key reference", "", "", "text", "", "", "", "",
                /*hidden=*/"true", /*readonly_=*/"");
            // Readonly column (timestamp)
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, kItems, "created_us", "Created", "Auto-generated timestamp", "", "", "text", "", "", "", "",
                /*hidden=*/"", /*readonly_=*/"true");

            TableHelpers::AdminEnumValues adminEnumValues(databaseHelper);
            TableHelpers::AdminColumnEnums adminColumnEnums(databaseHelper);

            EXPECT_THAT(
                helper.DatabaseMetadata(
                    transaction,
                    databaseInfo,
                    { "items" },
                    { "items" },
                    {},
                    adminTableFriendlyNames,
                    adminColumnFriendlyNames,
                    adminColumnDataInfo,
                    adminEnumValues,
                    adminColumnEnums),
                JsonValueIs(Json::Value::FromText(kTestDatabaseSchemaWithHiddenReadonly)));
            });
    }

    TEST(DatabaseRestHelperTest, DatabaseMetadataNoAllowedTables)
    {
        TestDatabaseUtil testDatabaseUtil(TestDatabaseUtil::SchemaMode::Empty);
        testDatabaseUtil.RunInTransaction("DatabaseMetadataNoAllowedTables", [&](Transaction& transaction) {
            DbSchema::DatabaseInfo databaseInfo = testDatabaseUtil.GetDatabaseInfo();
            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            DatabaseRESTHelper helper(databaseHelper);


            constexpr std::string_view kPeople = "people";
            databaseInfo.AddTable(kPeople);
            databaseInfo.AddColumnPrimaryKey(kPeople, "person_id", DB_TYPE_SERIAL);
            databaseInfo.AddColumnUnique(kPeople, "email", DB_TYPE_STRING);

            constexpr std::string_view kOrders = "orders";
            databaseInfo.AddTable(kOrders);
            databaseInfo.AddColumnPrimaryKey(kOrders, "order_id", DB_TYPE_SERIAL);
            databaseInfo.AddColumnForeignKeyRef(
                kPeople,
                "person_id",
                kOrders,
                "parent_person_id");

            TableHelpers::AdminTableFriendlyNames adminTableFriendlyNames(databaseHelper);
            TableHelpers::AdminColumnFriendlyNames adminColumnFriendlyNames(databaseHelper);
            TableHelpers::AdminColumnDataInfo adminColumnDataInfo(databaseHelper);
            TableHelpers::AdminEnumValues adminEnumValues(databaseHelper);
            TableHelpers::AdminColumnEnums adminColumnEnums(databaseHelper);

            EXPECT_THAT(
                helper.DatabaseMetadata(
                    transaction,
                    databaseInfo,
                    { },
                    {},
                    {},
                    adminTableFriendlyNames,
                    adminColumnFriendlyNames,
                    adminColumnDataInfo,
                    adminEnumValues,
                    adminColumnEnums),
				JsonValueIs(Json::Value::FromText(
					"{\"display_templates\":{},\"fk_picker_preload_threshold\":50,\"nested_tables\":[],\"tables\":[],\"root_tables\":[]}")));
            });
    }

    constexpr std::string_view kTestDatabaseSchemaWithEnum = R"JSON(
{
  "display_templates": {},
  "fk_picker_preload_threshold": 50,
  "nested_tables": [],
  "root_tables": ["widgets"],
  "tables": [
    {
      "columns": [
        {
          "column_friendly_name": "Widget ID",
          "column_name": "widget_id",
          "default_value": "",
          "hidden": "f",
          "hint": "",
          "html_input_type": "text",
          "label": "ID",
          "max_length": "",
          "nullable": "f",
          "place_holder": "",
          "primary_key": "t",
          "readonly": "f",
          "regex": "",
          "required": "",
          "rows": "",
          "type": "SERIAL",
          "unique": "f"
        },
        {
          "column_friendly_name": "Kind",
          "column_name": "kind",
          "default_value": "",
          "enum_values": ["alpha", "beta", "gamma"],
          "hidden": "f",
          "hint": "Widget kind",
          "html_input_type": "enum",
          "label": "Kind",
          "max_length": "",
          "nullable": "f",
          "place_holder": "",
          "primary_key": "f",
          "readonly": "f",
          "regex": "",
          "required": "t",
          "rows": "",
          "type": "VARCHAR",
          "unique": "f"
        }
      ],
      "description": "Test widgets",
      "foreign_keys": [],
      "primary_key": "widget_id",
      "table_friendly_name": "Widgets",
      "table_name": "widgets"
    }
  ]
}
)JSON";

    TEST(DatabaseRestHelperTest, DatabaseMetadataWithEnumColumn)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("DatabaseMetadataWithEnumColumn", [&](Transaction& transaction) {
            DbSchema::DatabaseInfo databaseInfo = testDatabaseUtil.GetDatabaseInfo();
            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            DatabaseRESTHelper helper(databaseHelper);

            constexpr std::string_view kWidgets = "widgets";
            databaseInfo.AddTable(kWidgets);
            databaseInfo.AddColumnPrimaryKey(kWidgets, "widget_id", DB_TYPE_SERIAL);
            databaseInfo.AddColumnSimple(kWidgets, "kind", DB_TYPE_STRING);

            TableHelpers::AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, kWidgets);

            TableHelpers::AdminTableFriendlyNames adminTableFriendlyNames(databaseHelper);
            adminTableFriendlyNames.AddAdminTableFriendlyName(
                transaction, kWidgets, "Widgets", "Test widgets");

            TableHelpers::AdminColumnFriendlyNames adminColumnFriendlyNames(databaseHelper);
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(transaction, kWidgets, "widget_id", "Widget ID");
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(transaction, kWidgets, "kind", "Kind");

            TableHelpers::AdminColumnDataInfo adminColumnDataInfo(databaseHelper);
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, kWidgets, "widget_id", "ID", "", "", "", "text");
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, kWidgets, "kind", "Kind", "Widget kind", "", "", "text", "true");

            // Look up the column_data_info_id for the "kind" column
            KeyValueTable kindColumnInfo;
            ASSERT_TRUE(adminColumnDataInfo.GetAdminColumnDataInfo(
                transaction, kWidgets, "kind", kindColumnInfo));
            int64_t kindColumnDataInfoId = std::stoll(kindColumnInfo.at(
                static_cast<std::string>(DbSchema::kAdminColumnDataInfoColumnDataInfoId)));

            // Create enum and values
            TableHelpers::AdminEnums adminEnums(databaseHelper);
            int64_t widgetKindEnumId = adminEnums.AddAdminEnum(transaction, "widget_kind");

            TableHelpers::AdminEnumValues adminEnumValues(databaseHelper);
            adminEnumValues.AddAdminEnumValue(transaction, widgetKindEnumId, "alpha", 0);
            adminEnumValues.AddAdminEnumValue(transaction, widgetKindEnumId, "beta", 1);
            adminEnumValues.AddAdminEnumValue(transaction, widgetKindEnumId, "gamma", 2);

            // Bind enum to column
            TableHelpers::AdminColumnEnums adminColumnEnums(databaseHelper);
            adminColumnEnums.AddAdminColumnEnum(transaction, widgetKindEnumId, kindColumnDataInfoId);

            EXPECT_THAT(
                helper.DatabaseMetadata(
                    transaction,
                    databaseInfo,
                    { "widgets" },
                    { "widgets" },
                    {},
                    adminTableFriendlyNames,
                    adminColumnFriendlyNames,
                    adminColumnDataInfo,
                    adminEnumValues,
                    adminColumnEnums),
                JsonValueIs(Json::Value::FromText(kTestDatabaseSchemaWithEnum)));
            });
    }

    constexpr std::string_view kTestDatabaseSchemaWithoutEnum = R"JSON(
{
  "display_templates": {},
  "fk_picker_preload_threshold": 50,
  "nested_tables": [],
  "root_tables": ["gadgets"],
  "tables": [
    {
      "columns": [
        {
          "column_friendly_name": "Gadget ID",
          "column_name": "gadget_id",
          "default_value": "",
          "hidden": "f",
          "hint": "",
          "html_input_type": "text",
          "label": "ID",
          "max_length": "",
          "nullable": "f",
          "place_holder": "",
          "primary_key": "t",
          "readonly": "f",
          "regex": "",
          "required": "",
          "rows": "",
          "type": "SERIAL",
          "unique": "f"
        },
        {
          "column_friendly_name": "Status",
          "column_name": "status",
          "default_value": "",
          "hidden": "f",
          "hint": "Gadget status",
          "html_input_type": "text",
          "label": "Status",
          "max_length": "",
          "nullable": "f",
          "place_holder": "",
          "primary_key": "f",
          "readonly": "f",
          "regex": "",
          "required": "t",
          "rows": "",
          "type": "VARCHAR",
          "unique": "f"
        }
      ],
      "description": "Test gadgets",
      "foreign_keys": [],
      "primary_key": "gadget_id",
      "table_friendly_name": "Gadgets",
      "table_name": "gadgets"
    }
  ]
}
)JSON";

    TEST(DatabaseRestHelperTest, DatabaseMetadataWithoutEnumColumn)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("DatabaseMetadataWithoutEnumColumn", [&](Transaction& transaction) {
            DbSchema::DatabaseInfo databaseInfo = testDatabaseUtil.GetDatabaseInfo();
            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            DatabaseRESTHelper helper(databaseHelper);

            constexpr std::string_view kGadgets = "gadgets";
            databaseInfo.AddTable(kGadgets);
            databaseInfo.AddColumnPrimaryKey(kGadgets, "gadget_id", DB_TYPE_SERIAL);
            databaseInfo.AddColumnSimple(kGadgets, "status", DB_TYPE_STRING);

            TableHelpers::AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, kGadgets);

            TableHelpers::AdminTableFriendlyNames adminTableFriendlyNames(databaseHelper);
            adminTableFriendlyNames.AddAdminTableFriendlyName(
                transaction, kGadgets, "Gadgets", "Test gadgets");

            TableHelpers::AdminColumnFriendlyNames adminColumnFriendlyNames(databaseHelper);
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(transaction, kGadgets, "gadget_id", "Gadget ID");
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(transaction, kGadgets, "status", "Status");

            TableHelpers::AdminColumnDataInfo adminColumnDataInfo(databaseHelper);
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, kGadgets, "gadget_id", "ID", "", "", "", "text");
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, kGadgets, "status", "Status", "Gadget status", "", "", "text", "true");

            // No enum binding created — status column should remain "text"
            TableHelpers::AdminEnumValues adminEnumValues(databaseHelper);
            TableHelpers::AdminColumnEnums adminColumnEnums(databaseHelper);

            EXPECT_THAT(
                helper.DatabaseMetadata(
                    transaction,
                    databaseInfo,
                    { "gadgets" },
                    { "gadgets" },
                    {},
                    adminTableFriendlyNames,
                    adminColumnFriendlyNames,
                    adminColumnDataInfo,
                    adminEnumValues,
                    adminColumnEnums),
                JsonValueIs(Json::Value::FromText(kTestDatabaseSchemaWithoutEnum)));
            });
    }

    TEST(DatabaseRestHelperTest, DatabaseMetadataWithDisplayTemplates)
    {
        TestDatabaseUtil testDatabaseUtil(TestDatabaseUtil::SchemaMode::Empty);
        testDatabaseUtil.RunInTransaction("DatabaseMetadataWithDisplayTemplates",
            [&](Transaction& transaction) {
            DbSchema::DatabaseInfo databaseInfo = testDatabaseUtil.GetDatabaseInfo();
            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

            DatabaseRESTHelper helper(databaseHelper);

            constexpr std::string_view kPeople = "people";
            databaseInfo.AddTable(kPeople);
            databaseInfo.AddColumnPrimaryKey(kPeople, "person_id", DB_TYPE_SERIAL);
            databaseInfo.AddColumnUnique(kPeople, "email", DB_TYPE_STRING);

            TableHelpers::AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, kPeople);

            TableHelpers::AdminTableFriendlyNames adminTableFriendlyNames(databaseHelper);
            adminTableFriendlyNames.AddAdminTableFriendlyName(
                transaction, kPeople, "People", "description");

            TableHelpers::AdminColumnFriendlyNames adminColumnFriendlyNames(databaseHelper);
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                transaction, kPeople, "person_id", "Person ID");
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                transaction, kPeople, "email", "Email");

            TableHelpers::AdminColumnDataInfo adminColumnDataInfo(databaseHelper);
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, kPeople, "person_id", "ID", "", "", "", "text");
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, kPeople, "email", "Email", "", "", "", "text");

            TableHelpers::AdminEnumValues adminEnumValues(databaseHelper);
            TableHelpers::AdminColumnEnums adminColumnEnums(databaseHelper);

            KeyValueTable displayTemplates = {
                {"people", "{first_name} {last_name}"},
                {"roles", "{name}"}
            };

            Json::Value result = helper.DatabaseMetadata(
                transaction,
                databaseInfo,
                { "people" },
                { "people" },
                {},
                adminTableFriendlyNames,
                adminColumnFriendlyNames,
                adminColumnDataInfo,
                adminEnumValues,
                adminColumnEnums,
                displayTemplates,
                100);

            // Verify display_templates
            const auto& templates = result["display_templates"].GetChildren();
            EXPECT_EQ(templates.at("people").Get<std::string>(),
                "{first_name} {last_name}");
            EXPECT_EQ(templates.at("roles").Get<std::string>(), "{name}");

            // Verify fk_picker_preload_threshold
            EXPECT_EQ(result["fk_picker_preload_threshold"].Get<int64_t>(), 100);
        });
    }

    void MakeFkOptionsTestTables(
        Transaction& transaction, TestDatabaseUtil& testDb) {
        auto databaseInfo = testDb.GetDatabaseInfo();
        auto databaseHelper = testDb.GetDatabaseHelper();
        constexpr std::string_view kPeople = "test_people";
        databaseInfo.AddTable(kPeople);
        databaseInfo.AddColumnPrimaryKey(kPeople, "person_id", DB_TYPE_SERIAL);
        databaseInfo.AddColumnUnique(kPeople, "email", DB_TYPE_STRING);
        databaseInfo.AddColumnSimple(kPeople, "first_name", DB_TYPE_STRING);
        databaseInfo.AddColumnSimple(kPeople, "last_name", DB_TYPE_STRING);
        DbOps::CreateTable(transaction, databaseInfo, kPeople);
    }

    int AddFkOptionsPerson(
        Transaction& transaction, TestDatabaseUtil& testDb,
        std::string_view email, std::string_view firstName,
        std::string_view lastName) {
        return DbCrud::AddRowToTableFetchIntPrimaryKey(
            transaction, testDb.GetDatabaseHelper(), "test_people",
            { {"email", std::string(email)},
              {"first_name", std::string(firstName)},
              {"last_name", std::string(lastName)} });
    }

    TEST(DatabaseRestHelperTest, GetFkOptionsEmptySearch) {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("GetFkOptionsEmptySearch",
            [&](Transaction& transaction) {
            MakeFkOptionsTestTables(transaction, testDb);
            DatabaseRESTHelper helper(testDb.GetDatabaseHelper());

            AddFkOptionsPerson(transaction, testDb,
                "alice@test.com", "Alice", "Smith");
            AddFkOptionsPerson(transaction, testDb,
                "bob@test.com", "Bob", "Jones");

            Json::Value result = helper.GetFkOptions(
                transaction, "test_people", "person_id",
                "{first_name} {last_name}", "", 50);

            EXPECT_EQ(result["total_count"].Get<int64_t>(), 2);
            const auto& options = result["options"].GetArray();
            ASSERT_EQ(options.size(), 2);

            for (const auto& option : options) {
                EXPECT_FALSE(option["value"].Get<std::string>().empty());
                EXPECT_FALSE(option["display"].Get<std::string>().empty());
            }
        });
    }

    TEST(DatabaseRestHelperTest, GetFkOptionsWithSearchText) {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("GetFkOptionsWithSearchText",
            [&](Transaction& transaction) {
            MakeFkOptionsTestTables(transaction, testDb);
            DatabaseRESTHelper helper(testDb.GetDatabaseHelper());

            AddFkOptionsPerson(transaction, testDb,
                "alice@test.com", "Alice", "Smith");
            AddFkOptionsPerson(transaction, testDb,
                "bob@test.com", "Bob", "Jones");
            AddFkOptionsPerson(transaction, testDb,
                "charlie@test.com", "Charlie", "Smith");

            Json::Value result = helper.GetFkOptions(
                transaction, "test_people", "person_id",
                "{first_name} {last_name}", "alice", 50);

            EXPECT_EQ(result["total_count"].Get<int64_t>(), 1);
            const auto& options = result["options"].GetArray();
            ASSERT_EQ(options.size(), 1);
            EXPECT_EQ(options[0]["display"].Get<std::string>(), "Alice Smith");
        });
    }

    TEST(DatabaseRestHelperTest, GetFkOptionsResolvedDisplayText) {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("GetFkOptionsResolvedDisplayText",
            [&](Transaction& transaction) {
            MakeFkOptionsTestTables(transaction, testDb);
            DatabaseRESTHelper helper(testDb.GetDatabaseHelper());

            int personId = AddFkOptionsPerson(transaction, testDb,
                "alice@test.com", "Alice", "Smith");

            Json::Value result = helper.GetFkOptions(
                transaction, "test_people", "person_id",
                "{first_name} {last_name} ({email})", "", 50);

            const auto& options = result["options"].GetArray();
            ASSERT_EQ(options.size(), 1);
            EXPECT_EQ(options[0]["value"].Get<std::string>(),
                std::to_string(personId));
            EXPECT_EQ(options[0]["display"].Get<std::string>(),
                "Alice Smith (alice@test.com)");
        });
    }

    TEST(DatabaseRestHelperTest, GetFkOptionsPageSizeLimit) {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("GetFkOptionsPageSizeLimit",
            [&](Transaction& transaction) {
            MakeFkOptionsTestTables(transaction, testDb);
            DatabaseRESTHelper helper(testDb.GetDatabaseHelper());

            AddFkOptionsPerson(transaction, testDb,
                "a@test.com", "Alice", "A");
            AddFkOptionsPerson(transaction, testDb,
                "b@test.com", "Bob", "B");
            AddFkOptionsPerson(transaction, testDb,
                "c@test.com", "Charlie", "C");

            Json::Value result = helper.GetFkOptions(
                transaction, "test_people", "person_id",
                "{first_name}", "", 2);

            EXPECT_EQ(result["total_count"].Get<int64_t>(), 3);
            const auto& options = result["options"].GetArray();
            EXPECT_EQ(options.size(), 2);
        });
    }

    TEST(DatabaseRestHelperTest, GetFkOptionsEmptyTemplate) {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("GetFkOptionsEmptyTemplate",
            [&](Transaction& transaction) {
            MakeFkOptionsTestTables(transaction, testDb);
            DatabaseRESTHelper helper(testDb.GetDatabaseHelper());

            int personId = AddFkOptionsPerson(transaction, testDb,
                "alice@test.com", "Alice", "Smith");

            Json::Value result = helper.GetFkOptions(
                transaction, "test_people", "person_id", "", "", 50);

            const auto& options = result["options"].GetArray();
            ASSERT_EQ(options.size(), 1);
            // No template — display should equal primary key value
            EXPECT_EQ(options[0]["display"].Get<std::string>(),
                std::to_string(personId));
            EXPECT_EQ(options[0]["value"].Get<std::string>(),
                std::to_string(personId));
        });
    }

    TEST(DatabaseRestHelperTest, GetFkOptionsSearchCaseInsensitive) {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("GetFkOptionsSearchCaseInsensitive",
            [&](Transaction& transaction) {
            MakeFkOptionsTestTables(transaction, testDb);
            DatabaseRESTHelper helper(testDb.GetDatabaseHelper());

            AddFkOptionsPerson(transaction, testDb,
                "alice@test.com", "Alice", "Smith");
            AddFkOptionsPerson(transaction, testDb,
                "bob@test.com", "Bob", "Jones");

            Json::Value result = helper.GetFkOptions(
                transaction, "test_people", "person_id",
                "{first_name} {last_name}", "ALICE", 50);

            EXPECT_EQ(result["total_count"].Get<int64_t>(), 1);
            const auto& options = result["options"].GetArray();
            ASSERT_EQ(options.size(), 1);
            EXPECT_EQ(options[0]["display"].Get<std::string>(), "Alice Smith");
        });
    }

    TEST(DatabaseRestHelperTest, GetFkOptionsNoResults) {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("GetFkOptionsNoResults",
            [&](Transaction& transaction) {
            MakeFkOptionsTestTables(transaction, testDb);
            DatabaseRESTHelper helper(testDb.GetDatabaseHelper());

            AddFkOptionsPerson(transaction, testDb,
                "alice@test.com", "Alice", "Smith");

            Json::Value result = helper.GetFkOptions(
                transaction, "test_people", "person_id",
                "{first_name}", "zzz_no_match", 50);

            EXPECT_EQ(result["total_count"].Get<int64_t>(), 0);
            const auto& options = result["options"].GetArray();
            EXPECT_EQ(options.size(), 0);
        });
    }

    TEST(DatabaseRestHelperTest, ResolveFkDisplayBasic) {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("ResolveFkDisplayBasic",
            [&](Transaction& transaction) {
            MakeFkOptionsTestTables(transaction, testDb);
            DatabaseRESTHelper helper(testDb.GetDatabaseHelper());

            int aliceId = AddFkOptionsPerson(transaction, testDb,
                "alice@test.com", "Alice", "Smith");
            int bobId = AddFkOptionsPerson(transaction, testDb,
                "bob@test.com", "Bob", "Jones");

            StringArray values = {
                std::to_string(aliceId), std::to_string(bobId) };
            Json::Value result = helper.ResolveFkDisplay(
                transaction, "test_people", "person_id",
                "{first_name} {last_name}", values);

            const auto& resolved = result["resolved"].GetChildren();
            EXPECT_EQ(resolved.at(std::to_string(aliceId)).Get<std::string>(),
                "Alice Smith");
            EXPECT_EQ(resolved.at(std::to_string(bobId)).Get<std::string>(),
                "Bob Jones");
        });
    }

    TEST(DatabaseRestHelperTest, ResolveFkDisplayEmptyValues) {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("ResolveFkDisplayEmptyValues",
            [&](Transaction& transaction) {
            MakeFkOptionsTestTables(transaction, testDb);
            DatabaseRESTHelper helper(testDb.GetDatabaseHelper());

            AddFkOptionsPerson(transaction, testDb,
                "alice@test.com", "Alice", "Smith");

            StringArray values = {};
            Json::Value result = helper.ResolveFkDisplay(
                transaction, "test_people", "person_id",
                "{first_name} {last_name}", values);

            const auto& resolved = result["resolved"].GetChildren();
            EXPECT_EQ(resolved.size(), 0);
        });
    }

    TEST(DatabaseRestHelperTest, ResolveFkDisplayEmptyTemplate) {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("ResolveFkDisplayEmptyTemplate",
            [&](Transaction& transaction) {
            MakeFkOptionsTestTables(transaction, testDb);
            DatabaseRESTHelper helper(testDb.GetDatabaseHelper());

            int aliceId = AddFkOptionsPerson(transaction, testDb,
                "alice@test.com", "Alice", "Smith");

            StringArray values = { std::to_string(aliceId) };
            Json::Value result = helper.ResolveFkDisplay(
                transaction, "test_people", "person_id", "", values);

            const auto& resolved = result["resolved"].GetChildren();
            std::string aliceIdStr = std::to_string(aliceId);
            EXPECT_EQ(resolved.at(aliceIdStr).Get<std::string>(), aliceIdStr);
        });
    }

    TEST(DatabaseRestHelperTest, ResolveFkDisplayPartialMatch) {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("ResolveFkDisplayPartialMatch",
            [&](Transaction& transaction) {
            MakeFkOptionsTestTables(transaction, testDb);
            DatabaseRESTHelper helper(testDb.GetDatabaseHelper());

            int aliceId = AddFkOptionsPerson(transaction, testDb,
                "alice@test.com", "Alice", "Smith");

            // Request includes a non-existent ID
            StringArray values = {
                std::to_string(aliceId), "99999" };
            Json::Value result = helper.ResolveFkDisplay(
                transaction, "test_people", "person_id",
                "{first_name} {last_name}", values);

            const auto& resolved = result["resolved"].GetChildren();
            // Only the existing ID should be resolved
            EXPECT_EQ(resolved.size(), 1);
            EXPECT_EQ(resolved.at(std::to_string(aliceId)).Get<std::string>(),
                "Alice Smith");
        });
    }

    TEST(DatabaseRestHelperTest, ResolveFkDisplayComplexTemplate) {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("ResolveFkDisplayComplexTemplate",
            [&](Transaction& transaction) {
            MakeFkOptionsTestTables(transaction, testDb);
            DatabaseRESTHelper helper(testDb.GetDatabaseHelper());

            int aliceId = AddFkOptionsPerson(transaction, testDb,
                "alice@test.com", "Alice", "Smith");

            StringArray values = { std::to_string(aliceId) };
            Json::Value result = helper.ResolveFkDisplay(
                transaction, "test_people", "person_id",
                "{first_name} {last_name} ({email})", values);

            const auto& resolved = result["resolved"].GetChildren();
            EXPECT_EQ(resolved.at(std::to_string(aliceId)).Get<std::string>(),
                "Alice Smith (alice@test.com)");
        });
    }

    TEST(DatabaseRestHelperTest, UpdateTableValueEmptyStringBecomesNull)
    {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("UpdateTableValueEmptyStringBecomesNull",
            [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            DatabaseRESTHelper helper(databaseHelper);

            // Insert a price_schedule with a valid_to_us value
            KeyValueTable insertRow = {
                {"name", "Test Schedule"},
                {"valid_from_us", "1000000"},
                {"valid_to_us", "2000000"},
                {"is_active", "true"},
            };
            int64_t scheduleId = DbCrud::AddRowToTableFetchInt64PrimaryKey(
                transaction, databaseHelper,
                DbSchema::kPriceSchedulesTable, insertRow);
            std::string scheduleIdStr = std::to_string(scheduleId);

            // Verify initial valid_to_us is set
            KeyValueTable initialRow = DbCrud::LookupRowByValue(
                transaction, databaseHelper,
                DbSchema::kPriceSchedulesTable,
                DbSchema::kPriceSchedulesId,
                scheduleIdStr);
            EXPECT_EQ(initialRow.at("valid_to_us"), "2000000");

            // Update with empty string for valid_to_us — should become NULL
            Json::Value updateJson(Json::JsonObject{
                {"name", "Updated Schedule"},
                {"valid_from_us", "1000000"},
                {"valid_to_us", ""},
                {"is_active", "true"},
            });

            helper.UpdateTableValue(
                transaction,
                testDb.GetDatabaseInfo(),
                DbSchema::kPriceSchedulesTable,
                DbSchema::kPriceSchedulesId,
                scheduleIdStr,
                updateJson);

            // Verify valid_to_us is now NULL (read back as empty string by
            // RunSqlStatementReturningOneRowHelper)
            KeyValueTable updatedRow = DbCrud::LookupRowByValue(
                transaction, databaseHelper,
                DbSchema::kPriceSchedulesTable,
                DbSchema::kPriceSchedulesId,
                scheduleIdStr);
            EXPECT_EQ(updatedRow.at("name"), "Updated Schedule");
            EXPECT_EQ(updatedRow.at("valid_to_us"), "");
        });
    }

    // ------------------------------------------------------------------
    // Phase 3.8 of the security review: column-level redact map.
    //
    // The DatabaseRESTHelper takes an optional ColumnRedactionSet at
    // construction time. Any (table, column) pair in that set is
    // dropped before the rows are converted to JSON. The set is the
    // last line of defence for the generic CRUD layer — credential
    // material like password_hash must never leak through it.
    // ------------------------------------------------------------------

    namespace {
        TableHelpers::ColumnRedactionSet PeoplePasswordHashRedaction() {
            return {
                {
                    std::string(DbSchema::kPeopleTable),
                    std::string(DbSchema::kPeoplePasswordHash)
                }
            };
        }

        // Helper: scan a "sortedColumnNames + dataTable" Json::Value and
        // assert that the named column is absent, and that the row
        // count matches expected.
        void ExpectColumnAbsent(
            const Json::Value& tableResult, std::string_view columnName) {
            ASSERT_TRUE(tableResult.HasChild("sortedColumnNames", nullptr));
            const auto& cols = tableResult["sortedColumnNames"].GetArray();
            for (size_t i = 0; i < cols.size(); ++i) {
                EXPECT_NE(cols[i].Get<std::string>(), columnName)
                    << "Column '" << columnName
                    << "' should have been redacted but appeared at index "
                    << i;
            }
        }

        void ExpectColumnPresent(
            const Json::Value& tableResult, std::string_view columnName) {
            ASSERT_TRUE(tableResult.HasChild("sortedColumnNames", nullptr));
            const auto& cols = tableResult["sortedColumnNames"].GetArray();
            bool found = false;
            for (size_t i = 0; i < cols.size(); ++i) {
                if (cols[i].Get<std::string>() == columnName) {
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found)
                << "Column '" << columnName
                << "' was expected to be present but is missing";
        }
    }

    TEST(DatabaseRestHelperTest, JsonFromDataResultsRedactsConfiguredColumns) {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction(
            "JsonFromDataResultsRedactsConfiguredColumns",
            [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                int64_t id1 = testDatabaseUtil.AddPerson(
                    transaction, "redact1@example.com", "f1", "l1", "secret1");
                int64_t id2 = testDatabaseUtil.AddPerson(
                    transaction, "redact2@example.com", "f2", "l2", "secret2");
                (void)id1;
                (void)id2;

                DatabaseRESTHelper helper(
                    databaseHelper, PeoplePasswordHashRedaction());

                auto tableResult = helper.GetTableValues(
                    transaction, DbSchema::kPeopleTable);

                // password_hash is gone, but the rows themselves are still here.
                ASSERT_EQ(tableResult["dataTable"].GetArray().size(), 2u);
                ExpectColumnAbsent(tableResult, "password_hash");
                ExpectColumnPresent(tableResult, "email");
                ExpectColumnPresent(tableResult, "first_name");
                ExpectColumnPresent(tableResult, "last_name");
                ExpectColumnPresent(tableResult, "id");

                // Confirm no row's data array contains the secret values
                // anywhere — defends against a future bug that strips the
                // column header but leaves the value.
                for (const auto& row : tableResult["dataTable"].GetArray()) {
                    for (const auto& cell : row.GetArray()) {
                        std::string s = cell.Is<std::string>()
                            ? cell.Get<std::string>()
                            : cell.ToSimpleString();
                        EXPECT_NE(s, "secret1");
                        EXPECT_NE(s, "secret2");
                    }
                }
            });
    }

    // Negative test: with no redactions configured, the helper still
    // emits the column as before. Catches a bug where the filter is
    // accidentally applied even when the set is empty.
    TEST(DatabaseRestHelperTest, JsonFromDataResultsKeepsNonRedactedColumns) {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction(
            "JsonFromDataResultsKeepsNonRedactedColumns",
            [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                int64_t id = testDatabaseUtil.AddPerson(
                    transaction, "keep@example.com", "f", "l", "h");
                (void)id;

                // No redactions in the set -> all columns should appear.
                DatabaseRESTHelper helper(databaseHelper, {});
                auto tableResult = helper.GetTableValues(
                    transaction, DbSchema::kPeopleTable);

                ExpectColumnPresent(tableResult, "password_hash");
                ExpectColumnPresent(tableResult, "email");
            });
    }

    // GetRow is a different code path (single row, MakeDataResults
    // instead of MakeDataResultsFromKeyValueTableArray). Cover it
    // explicitly so a future refactor can't drop the redaction call.
    TEST(DatabaseRestHelperTest, GetRowRedactsConfiguredColumns) {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction(
            "GetRowRedactsConfiguredColumns",
            [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                int64_t id = testDatabaseUtil.AddPerson(
                    transaction, "single@example.com", "f", "l", "single-hash");

                DatabaseRESTHelper helper(
                    databaseHelper, PeoplePasswordHashRedaction());

                auto rowResult = helper.GetRow(
                    transaction,
                    DbSchema::kPeopleTable,
                    DbSchema::kPeopleId,
                    StringFromInt(id));

                ExpectColumnAbsent(rowResult, "password_hash");
                ExpectColumnPresent(rowResult, "email");
                for (const auto& row : rowResult["dataTable"].GetArray()) {
                    for (const auto& cell : row.GetArray()) {
                        std::string s = cell.Is<std::string>()
                            ? cell.Get<std::string>()
                            : cell.ToSimpleString();
                        EXPECT_NE(s, "single-hash");
                    }
                }
            });
    }

    // GetRowByValues is also distinct — uses LookupRowByValues.
    TEST(DatabaseRestHelperTest, GetRowByValuesRedactsConfiguredColumns) {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction(
            "GetRowByValuesRedactsConfiguredColumns",
            [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                int64_t id = testDatabaseUtil.AddPerson(
                    transaction, "byval@example.com", "f", "l", "byval-hash");
                (void)id;

                DatabaseRESTHelper helper(
                    databaseHelper, PeoplePasswordHashRedaction());

                KeyValueTable lookup = {
                    { std::string(DbSchema::kPeopleEmail), "byval@example.com" }
                };
                auto rowResult = helper.GetRowByValues(
                    transaction, DbSchema::kPeopleTable, lookup);

                ExpectColumnAbsent(rowResult, "password_hash");
                for (const auto& row : rowResult["dataTable"].GetArray()) {
                    for (const auto& cell : row.GetArray()) {
                        std::string s = cell.Is<std::string>()
                            ? cell.Get<std::string>()
                            : cell.ToSimpleString();
                        EXPECT_NE(s, "byval-hash");
                    }
                }
            });
    }

    // Redactions are scoped to the (table, column) pair. Using the
    // same column name on a different table must NOT redact.
    TEST(DatabaseRestHelperTest, RedactionsAreTableScoped) {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction(
            "RedactionsAreTableScoped",
            [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

                // Add a redaction for ('other_table', 'password_hash').
                // The people.password_hash column must be unaffected.
                TableHelpers::ColumnRedactionSet otherTableOnly = {
                    { std::string("other_table"),
                      std::string(DbSchema::kPeoplePasswordHash) }
                };

                int64_t id = testDatabaseUtil.AddPerson(
                    transaction, "scope@example.com", "f", "l", "scope-hash");
                (void)id;

                DatabaseRESTHelper helper(databaseHelper, otherTableOnly);
                auto tableResult = helper.GetTableValues(
                    transaction, DbSchema::kPeopleTable);

                ExpectColumnPresent(tableResult, "password_hash");
            });
    }

} // namespace {

