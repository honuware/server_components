#include "sql_util/table_helpers/admin_top_level_tables.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "util/file_util.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"
#include "db_schema/admin_top_level_tables.h"

namespace TableHelpers {
namespace {

using ::testing::UnorderedElementsAre;

TEST(AdminTopLevelTablesTest, AddAdminTopLevelTablesAndGetAdminTopLevelTablesBasic)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("AddAndGetAdminTopLevelTablesBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        AdminTopLevelTables adminTopLevelTables(databaseHelper);
        adminTopLevelTables.AddAdminTopLevelTable(transaction, "table1");
        adminTopLevelTables.AddAdminTopLevelTable(transaction, "table2");

        EXPECT_THAT(
            adminTopLevelTables.GetAdminTopLevelTables(transaction), 
            UnorderedElementsAre("table1", "table2"));
        });
}

TEST(AdminTopLevelTablesTest, DeleteAdminTopLevelTableBasic)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("DeleteAdminTopLevelTableBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
        AdminTopLevelTables adminTopLevelTables(databaseHelper);
        adminTopLevelTables.AddAdminTopLevelTable(transaction, "table1");
        adminTopLevelTables.AddAdminTopLevelTable(transaction, "table2");
        EXPECT_THAT(
            adminTopLevelTables.GetAdminTopLevelTables(transaction),
            UnorderedElementsAre("table1", "table2"));
        adminTopLevelTables.DeleteAdminTopLevelTable(transaction, "table1");
        EXPECT_THAT(
            adminTopLevelTables.GetAdminTopLevelTables(transaction),
            UnorderedElementsAre("table2"));
        adminTopLevelTables.DeleteAdminTopLevelTable(transaction, "table2");
        EXPECT_THAT(
            adminTopLevelTables.GetAdminTopLevelTables(transaction),
            UnorderedElementsAre());
        });
}

TEST(AdminTopLevelTablesTest, IsAdminTopLevelTablesBasic)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("IsAdminTopLevelTablesBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        AdminTopLevelTables adminTopLevelTables(databaseHelper);
        ASSERT_FALSE(adminTopLevelTables.IsTableAdminTopLevel(transaction, "table1"));
        adminTopLevelTables.AddAdminTopLevelTable(transaction, "table1");
        ASSERT_TRUE(adminTopLevelTables.IsTableAdminTopLevel(transaction, "table1"));
        });
}

} // namespace {
} // namespace TableHelpers {