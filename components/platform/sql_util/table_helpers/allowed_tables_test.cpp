#include "allowed_tables.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "util/file_util.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"
#include "db_schema/allowed_tables.h"

namespace TableHelpers {
namespace {

using ::testing::UnorderedElementsAre;

TEST(AddAllowedTableTest, AddAllowedTableAndGetAllowedTablesBasic)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("AddAllowedTableAndGetAllowedTablesBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        AllowedTables allowedTables(databaseHelper);
        allowedTables.AddAllowedTable(transaction, "table1");
        allowedTables.AddAllowedTable(transaction, "table2");

        EXPECT_THAT(
            allowedTables.GetAllowedTables(transaction), 
            UnorderedElementsAre("table1", "table2"));
        });
}

TEST(AddAllowedTableTest, DeleteAllowedTableBasic)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("DeleteAllowedTableBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        AllowedTables allowedTables(databaseHelper);
        allowedTables.AddAllowedTable(transaction, "table1");
        allowedTables.AddAllowedTable(transaction, "table2");

        EXPECT_THAT(
            allowedTables.GetAllowedTables(transaction),
            UnorderedElementsAre("table1", "table2"));
        allowedTables.DeleteAllowedTable(transaction, "table1");
        EXPECT_THAT(
            allowedTables.GetAllowedTables(transaction),
            UnorderedElementsAre("table2"));
        allowedTables.DeleteAllowedTable(transaction, "table2");
        EXPECT_THAT(
            allowedTables.GetAllowedTables(transaction),
            UnorderedElementsAre());
        });
}

TEST(AddAllowedTableTest, IsTableAllowedBasic)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("IsTableAllowedBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        AllowedTables allowedTables(databaseHelper);
        ASSERT_FALSE(allowedTables.IsTableAllowed(transaction, "table1"));
        allowedTables.AddAllowedTable(transaction, "table1");
        ASSERT_TRUE(allowedTables.IsTableAllowed(transaction, "table1"));
        });
}

} // namespace {
} // namespace TableHelpers {