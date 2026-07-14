#include "sql_util/table_helpers/admin_column_enums.h"
#include "sql_util/table_helpers/admin_enums.h"
#include "sql_util/table_helpers/admin_column_data_info.h"
#include "sql_util/table_helpers/admin_top_level_tables.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"
#include "db_schema/admin_enums.h"
#include "db_schema/admin_column_enums.h"
#include "db_schema/admin_column_data_info.h"

namespace TableHelpers {
namespace {
    using ::testing::UnorderedElementsAre;
    using ::testing::ContainerEq;

    // Helper to get the column_data_info_id for a given column name
    int64_t GetColumnDataInfoId(AdminColumnDataInfo& info, Transaction& transaction,
        const std::string& columnName) {
        auto rows = info.GetAdminColumnDataInfos(transaction);
        for (const auto& row : rows) {
            if (row.at(std::string(DbSchema::kAdminColumnDataInfoColumnName)) == columnName) {
                return std::stoll(row.at(std::string(DbSchema::kAdminColumnDataInfoColumnDataInfoId)));
            }
        }
        return -1;
    }

    TEST(AdminColumnEnumsTest, AddAdminColumnEnumBasic)
    {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("AddAdminColumnEnumBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "products");
            AdminColumnDataInfo adminColumnDataInfo(databaseHelper);
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, "products", "kind", "Kind", "Product type", "", "", "text", "true");
            int64_t kindDataInfoId = GetColumnDataInfoId(adminColumnDataInfo, transaction, "kind");

            AdminEnums adminEnums(databaseHelper);
            int64_t enumId = adminEnums.AddAdminEnum(transaction, "product_kind");

            AdminColumnEnums adminColumnEnums(databaseHelper);
            adminColumnEnums.AddAdminColumnEnum(transaction, enumId, kindDataInfoId);

            KeyValueTable result;
            ASSERT_TRUE(adminColumnEnums.GetAdminColumnEnumByColumnDataInfoId(transaction, kindDataInfoId, result));
            EXPECT_EQ(result.at(std::string(DbSchema::kAdminColumnEnumsAdminEnumId)),
                std::to_string(enumId));
        });
    }

    TEST(AdminColumnEnumsTest, GetAdminColumnEnumNotFound)
    {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("GetAdminColumnEnumNotFound", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

            AdminColumnEnums adminColumnEnums(databaseHelper);
            KeyValueTable result;
            ASSERT_FALSE(adminColumnEnums.GetAdminColumnEnumByColumnDataInfoId(transaction, 999, result));
        });
    }

    TEST(AdminColumnEnumsTest, GetAdminColumnEnums)
    {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("GetAdminColumnEnums", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "products");
            AdminColumnDataInfo adminColumnDataInfo(databaseHelper);
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, "products", "kind", "Kind", "Product type", "", "", "text", "true");
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, "products", "is_active", "Active", "Whether active", "", "", "bool", "true");
            int64_t kindDataInfoId = GetColumnDataInfoId(adminColumnDataInfo, transaction, "kind");
            int64_t activeDataInfoId = GetColumnDataInfoId(adminColumnDataInfo, transaction, "is_active");

            AdminEnums adminEnums(databaseHelper);
            int64_t productKindId = adminEnums.AddAdminEnum(transaction, "product_kind");
            int64_t boolEnumId = adminEnums.AddAdminEnum(transaction, "test_bool_enum");

            AdminColumnEnums adminColumnEnums(databaseHelper);
            adminColumnEnums.AddAdminColumnEnum(transaction, productKindId, kindDataInfoId);
            adminColumnEnums.AddAdminColumnEnum(transaction, boolEnumId, activeDataInfoId);

            KeyValueTableArray all = adminColumnEnums.GetAdminColumnEnums(transaction);
            EXPECT_EQ(all.size(), 2);
        });
    }

    TEST(AdminColumnEnumsTest, DeleteAdminColumnEnumBasic)
    {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("DeleteAdminColumnEnumBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "products");
            AdminColumnDataInfo adminColumnDataInfo(databaseHelper);
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, "products", "kind", "Kind", "Product type", "", "", "text", "true");
            adminColumnDataInfo.AddAdminColumnDataInfo(
                transaction, "products", "is_active", "Active", "Whether active", "", "", "bool", "true");
            int64_t kindDataInfoId = GetColumnDataInfoId(adminColumnDataInfo, transaction, "kind");
            int64_t activeDataInfoId = GetColumnDataInfoId(adminColumnDataInfo, transaction, "is_active");

            AdminEnums adminEnums(databaseHelper);
            int64_t productKindId = adminEnums.AddAdminEnum(transaction, "product_kind");
            int64_t boolEnumId = adminEnums.AddAdminEnum(transaction, "test_bool_enum");

            AdminColumnEnums adminColumnEnums(databaseHelper);
            adminColumnEnums.AddAdminColumnEnum(transaction, productKindId, kindDataInfoId);
            adminColumnEnums.AddAdminColumnEnum(transaction, boolEnumId, activeDataInfoId);
            EXPECT_EQ(adminColumnEnums.GetAdminColumnEnums(transaction).size(), 2);

            // Get the id of the first binding to delete it
            KeyValueTable binding;
            ASSERT_TRUE(adminColumnEnums.GetAdminColumnEnumByColumnDataInfoId(transaction, kindDataInfoId, binding));
            int64_t bindingId = std::stoll(binding.at(std::string(DbSchema::kAdminColumnEnumsId)));

            adminColumnEnums.DeleteAdminColumnEnum(transaction, bindingId);
            EXPECT_EQ(adminColumnEnums.GetAdminColumnEnums(transaction).size(), 1);

            KeyValueTable result;
            ASSERT_FALSE(adminColumnEnums.GetAdminColumnEnumByColumnDataInfoId(transaction, kindDataInfoId, result));
            ASSERT_TRUE(adminColumnEnums.GetAdminColumnEnumByColumnDataInfoId(transaction, activeDataInfoId, result));
        });
    }

} // namespace
}  // namespace TableHelpers
