#include "sql_util/table_helpers/admin_nested_tables.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "util/file_util.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"
#include "db_schema/admin_nested_tables.h"

namespace TableHelpers {
namespace {

using ::testing::UnorderedElementsAre;

TEST(AdminNestedTablesTest, AddAndGetAdminNestedTablesBasic)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("AddAndGetAdminNestedTablesBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        AdminNestedTables adminNestedTables(databaseHelper);
        adminNestedTables.AddAdminNestedTable(transaction, "table1");
        adminNestedTables.AddAdminNestedTable(transaction, "table2");

        EXPECT_THAT(
            adminNestedTables.GetAdminNestedTables(transaction),
            UnorderedElementsAre("table1", "table2"));
        });
}

TEST(AdminNestedTablesTest, DeleteAdminNestedTableBasic)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("DeleteAdminNestedTableBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        AdminNestedTables adminNestedTables(databaseHelper);
        adminNestedTables.AddAdminNestedTable(transaction, "table1");
        adminNestedTables.AddAdminNestedTable(transaction, "table2");
        EXPECT_THAT(
            adminNestedTables.GetAdminNestedTables(transaction),
            UnorderedElementsAre("table1", "table2"));

        adminNestedTables.DeleteAdminNestedTable(transaction, "table1");
        EXPECT_THAT(
            adminNestedTables.GetAdminNestedTables(transaction),
            UnorderedElementsAre("table2"));

        adminNestedTables.DeleteAdminNestedTable(transaction, "table2");
        EXPECT_THAT(
            adminNestedTables.GetAdminNestedTables(transaction),
            UnorderedElementsAre());
        });
}

TEST(AdminNestedTablesTest, IsNestedTableBasic)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("IsNestedTableBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        AdminNestedTables adminNestedTables(databaseHelper);
        ASSERT_FALSE(adminNestedTables.IsNestedTable(transaction, "table1"));
        adminNestedTables.AddAdminNestedTable(transaction, "table1");
        ASSERT_TRUE(adminNestedTables.IsNestedTable(transaction, "table1"));
        ASSERT_FALSE(adminNestedTables.IsNestedTable(transaction, "table2"));
        });
}

} // namespace
} // namespace TableHelpers
