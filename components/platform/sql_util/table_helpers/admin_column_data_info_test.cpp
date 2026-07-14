#include "sql_util/table_helpers/admin_column_data_info.h"
#include "sql_util/table_helpers/admin_top_level_tables.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "util/file_util.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"
#include "db_schema/admin_column_data_info.h"

namespace TableHelpers {
namespace {
    using ::testing::UnorderedElementsAre;
    using ::testing::ElementsAre;
    using ::testing::Pair;
    using ::testing::ContainerEq;

    KeyValueTable MakeDataInfo(
        std::string_view id,
        std::string_view tableName,
        std::string_view columnName,
        std::string_view label,
        std::string_view hint = "",
        std::string_view placeHolder = "",
        std::string_view regex = "",
        std::string_view htmlInputType = "",
        std::string_view required = "",
        std::string_view maxLength = "",
        std::string_view defaultValue = "",
        std::string_view rows = "",
        std::string_view hidden = "",
        std::string_view readonly_ = "") {
        return MakeKeyValueTable({
            {DbSchema::kAdminColumnDataInfoColumnDataInfoId, id},
            {DbSchema::kAdminColumnDataInfoTableName, tableName},
            {DbSchema::kAdminColumnDataInfoColumnName, columnName},
            {DbSchema::kAdminColumnDataInfoLabel, label},
            {DbSchema::kAdminColumnDataInfoHint, hint},
            {DbSchema::kAdminColumnDataInfoPlaceHolder, placeHolder},
            {DbSchema::kAdminColumnDataInfoRegex, regex},
            {DbSchema::kAdminColumnDataInfoHtmlInputType, htmlInputType},
            {DbSchema::kAdminColumnDataInfoRequired, required},
            {DbSchema::kAdminColumnDataInfoMaxLength, maxLength},
            {DbSchema::kAdminColumnDataInfoDefaultValue, defaultValue},
            {DbSchema::kAdminColumnDataInfoRows, rows},
            {DbSchema::kAdminColumnDataInfoHidden, hidden},
            {DbSchema::kAdminColumnDataInfoReadonly, readonly_}});
    }

    KeyValueTable MakeDataInfo1(std::string_view id) {
        return MakeDataInfo(id, "table1", "column1", "label1", "hint1", "placeHolder1", "regex1", "htmlInputType1", "t", "5", "default1", "3");
    }

    KeyValueTable MakeDataInfo2(std::string_view id) {
        return MakeDataInfo(id, "table2", "column2", "label2", "hint2", "placeHolder2", "regex2", "htmlInputType2", "f", "6", "default2", "4");
    }

    // Look up actual IDs from the database after insertion
    std::pair<std::string, std::string> GetDataInfoIds(
        AdminColumnDataInfo& adminColumnDataInfo, Transaction& transaction) {
        auto rows = adminColumnDataInfo.GetAdminColumnDataInfos(transaction);
        std::string id1, id2;
        for (const auto& row : rows) {
            if (row.at(std::string(DbSchema::kAdminColumnDataInfoColumnName)) == "column1") {
                id1 = row.at(std::string(DbSchema::kAdminColumnDataInfoColumnDataInfoId));
            } else if (row.at(std::string(DbSchema::kAdminColumnDataInfoColumnName)) == "column2") {
                id2 = row.at(std::string(DbSchema::kAdminColumnDataInfoColumnDataInfoId));
            }
        }
        return {id1, id2};
    }

    TEST(AdminColumnDataInfoTest, AddAdminColumnDataInfoBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("AddAdminColumnDataInfoBasic", [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
            AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "table1");
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "table2");
            AdminColumnDataInfo adminColumnDataInfo(databaseHelper);
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, "table1", "column1", "label1", "hint1", "placeHolder1", "regex1", "htmlInputType1", "true", "5", "default1", "3");
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, "table2", "column2", "label2", "hint2", "placeHolder2", "regex2", "htmlInputType2", "false", "6", "default2", "4");
            auto [id1, id2] = GetDataInfoIds(adminColumnDataInfo, transaction);
            EXPECT_THAT(adminColumnDataInfo.GetAdminColumnDataInfos(transaction),
                UnorderedElementsAre(
                    ContainerEq(MakeDataInfo1(id1)),
                    ContainerEq(MakeDataInfo2(id2))));
            });
    }

    TEST(AdminColumnDataInfoTest, DeleteAdminColumnDataInfoBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("DeleteAdminColumnDataInfoBasic", [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
            AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "table1");
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "table2");
            AdminColumnDataInfo adminColumnDataInfo(databaseHelper);
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, "table1", "column1", "label1", "hint1", "placeHolder1", "regex1", "htmlInputType1", "true", "5", "default1", "3");
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, "table2", "column2", "label2", "hint2", "placeHolder2", "regex2", "htmlInputType2", "false", "6", "default2", "4");
            auto [id1, id2] = GetDataInfoIds(adminColumnDataInfo, transaction);
            EXPECT_THAT(adminColumnDataInfo.GetAdminColumnDataInfos(transaction),
                UnorderedElementsAre(
                    ContainerEq(MakeDataInfo1(id1)),
                    ContainerEq(MakeDataInfo2(id2))));
            adminColumnDataInfo.DeleteAdminColumnDataInfo(transaction, "table1", "column1");
            EXPECT_THAT(adminColumnDataInfo.GetAdminColumnDataInfos(transaction),
                UnorderedElementsAre(
                    ContainerEq(MakeDataInfo2(id2))));
            adminColumnDataInfo.DeleteAdminColumnDataInfo(transaction, "table2", "column2");
            EXPECT_THAT(adminColumnDataInfo.GetAdminColumnDataInfos(transaction), UnorderedElementsAre());
            });
    }

    TEST(AdminColumnDataInfoTest, GetAdminColumnDataInfoBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("GetAdminColumnDataInfoBasic", [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
            AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "table1");
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "table2");
            AdminColumnDataInfo adminColumnDataInfo(databaseHelper);
            KeyValueTable keyValueTable;
            ASSERT_FALSE(adminColumnDataInfo.GetAdminColumnDataInfo(transaction, "table1", "column1", keyValueTable));
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, "table1", "column1", "label1", "hint1", "placeHolder1", "regex1", "htmlInputType1", "true", "5", "default1", "3");
            ASSERT_TRUE(adminColumnDataInfo.GetAdminColumnDataInfo(transaction, "table1", "column1", keyValueTable));
            std::string actualId = keyValueTable.at(std::string(DbSchema::kAdminColumnDataInfoColumnDataInfoId));
            EXPECT_THAT(keyValueTable, ContainerEq(MakeDataInfo1(actualId)));
            });
    }

    TEST(AdminColumnDataInfoTest, AddAdminColumnDataInfoWithHiddenAndReadonly)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("AddAdminColumnDataInfoWithHiddenAndReadonly", [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
            AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "table1");
            AdminColumnDataInfo adminColumnDataInfo(databaseHelper);

            // Add a hidden column
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, "table1", "fk_column", "FK Column", "Foreign key", "", "", "text", "false", "", "", "",
                /*hidden=*/"true", /*readonly_=*/"");

            // Add a readonly column
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, "table1", "created_us", "Created", "Auto-generated timestamp", "", "", "text", "false", "", "", "",
                /*hidden=*/"", /*readonly_=*/"true");

            // Add a column with both hidden and readonly
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, "table1", "internal_col", "Internal", "Internal column", "", "", "text", "false", "", "", "",
                /*hidden=*/"true", /*readonly_=*/"true");

            KeyValueTable hiddenResult;
            ASSERT_TRUE(adminColumnDataInfo.GetAdminColumnDataInfo(transaction, "table1", "fk_column", hiddenResult));
            std::string hiddenId = hiddenResult.at(std::string(DbSchema::kAdminColumnDataInfoColumnDataInfoId));
            EXPECT_THAT(hiddenResult, ContainerEq(MakeDataInfo(
                hiddenId, "table1", "fk_column", "FK Column", "Foreign key", "", "", "text", "f", "", "", "", "t", "")));

            KeyValueTable readonlyResult;
            ASSERT_TRUE(adminColumnDataInfo.GetAdminColumnDataInfo(transaction, "table1", "created_us", readonlyResult));
            std::string readonlyId = readonlyResult.at(std::string(DbSchema::kAdminColumnDataInfoColumnDataInfoId));
            EXPECT_THAT(readonlyResult, ContainerEq(MakeDataInfo(
                readonlyId, "table1", "created_us", "Created", "Auto-generated timestamp", "", "", "text", "f", "", "", "", "", "t")));

            KeyValueTable bothResult;
            ASSERT_TRUE(adminColumnDataInfo.GetAdminColumnDataInfo(transaction, "table1", "internal_col", bothResult));
            std::string bothId = bothResult.at(std::string(DbSchema::kAdminColumnDataInfoColumnDataInfoId));
            EXPECT_THAT(bothResult, ContainerEq(MakeDataInfo(
                bothId, "table1", "internal_col", "Internal", "Internal column", "", "", "text", "f", "", "", "", "t", "t")));
            });
    }

} // namespace {
}  // namespace TableHelpers {