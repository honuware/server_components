#include "sql_util/table_helpers/admin_column_friendly_names.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "util/file_util.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"
#include "db_schema/admin_column_friendly_names.h"
#include "sql_util/table_helpers/admin_top_level_tables.h"

namespace TableHelpers {
namespace {
    using ::testing::UnorderedElementsAre;
    using ::testing::ElementsAre;
    using ::testing::Pair;
    using ::testing::ContainerEq;

    KeyValueTable MakeFriendly(const std::string& id, const std::string& tableName, const std::string& columnName, const std::string& friendlyName) {
        return KeyValueTable{
            std::make_pair((std::string)DbSchema::kAdminColumnFriendlyNamesColumnFriendlyNameId, id),
            std::make_pair((std::string)DbSchema::kAdminColumnFriendlyNamesTableName, tableName),
            std::make_pair((std::string)DbSchema::kAdminColumnFriendlyNamesColumnName, columnName),
            std::make_pair((std::string)DbSchema::kAdminColumnFriendlyNamesFriendlyName, friendlyName) };
    }

    KeyValueTable MakeFriendl1(const std::string& id) {
        return MakeFriendly(id, "table1", "column1", "friendly1");
    }

    KeyValueTable MakeFriendl2(const std::string& id) {
        return MakeFriendly(id, "table2", "column2", "friendly2");
    }

    std::pair<std::string, std::string> GetFriendlyNameIds(
        AdminColumnFriendlyNames& names, Transaction& transaction) {
        auto rows = names.GetAdminColumnFriendlyNames(transaction);
        std::string id1, id2;
        for (const auto& row : rows) {
            if (row.at(std::string(DbSchema::kAdminColumnFriendlyNamesColumnName)) == "column1") {
                id1 = row.at(std::string(DbSchema::kAdminColumnFriendlyNamesColumnFriendlyNameId));
            } else if (row.at(std::string(DbSchema::kAdminColumnFriendlyNamesColumnName)) == "column2") {
                id2 = row.at(std::string(DbSchema::kAdminColumnFriendlyNamesColumnFriendlyNameId));
            }
        }
        return {id1, id2};
    }

    TEST(AdminColumnFriendlyNamesTest, AddAdminColumnFriendlyNameBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("AdminColumnFriendlyNamesTest.AddAdminColumnFriendlyNameBasic", [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
            AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "table1");
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "table2");

            AdminColumnFriendlyNames adminColumnFriendlyNames(databaseHelper);
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                transaction, "table1", "column1", "friendly1");
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                transaction, "table2", "column2", "friendly2");

            auto [id1, id2] = GetFriendlyNameIds(adminColumnFriendlyNames, transaction);
            EXPECT_THAT(adminColumnFriendlyNames.GetAdminColumnFriendlyNames(transaction),
                UnorderedElementsAre(
                    ContainerEq(MakeFriendl1(id1)),
                    ContainerEq(MakeFriendl2(id2))));
            });
    }

    TEST(AdminColumnFriendlyNamesTest, DeleteAdminColumnFriendlyNameBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("AdminColumnFriendlyNamesTest.DeleteAdminColumnFriendlyNameBasic", [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
            AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "table1");
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "table2");
            AdminColumnFriendlyNames adminColumnFriendlyNames(databaseHelper);
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                transaction, "table1", "column1", "friendly1");
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                transaction, "table2", "column2", "friendly2");

            auto [id1, id2] = GetFriendlyNameIds(adminColumnFriendlyNames, transaction);
            EXPECT_THAT(adminColumnFriendlyNames.GetAdminColumnFriendlyNames(transaction),
                UnorderedElementsAre(
                    ContainerEq(MakeFriendl1(id1)),
                    ContainerEq(MakeFriendl2(id2))));
            adminColumnFriendlyNames.DeleteAdminColumnFriendlyName(transaction, "table1", "column1");
            EXPECT_THAT(adminColumnFriendlyNames.GetAdminColumnFriendlyNames(transaction),
                UnorderedElementsAre(
                    ContainerEq(MakeFriendl2(id2))));
            adminColumnFriendlyNames.DeleteAdminColumnFriendlyName(transaction, "table2", "column2");
            EXPECT_THAT(adminColumnFriendlyNames.GetAdminColumnFriendlyNames(transaction), UnorderedElementsAre());
            });
    }

    TEST(AdminColumnFriendlyNamesTest, GetAdminColumnFriendlyNameBasic)
    {
        TestDatabaseUtil testDatabaseUtil;
        testDatabaseUtil.RunInTransaction("AdminColumnFriendlyNamesTest.GetAdminColumnFriendlyNameBasic", [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
            AdminTopLevelTables adminTopLevelTables(databaseHelper);
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "table1");
            adminTopLevelTables.AddAdminTopLevelTable(transaction, "table2");

            AdminColumnFriendlyNames adminColumnFriendlyNames(databaseHelper);
            EXPECT_TRUE(adminColumnFriendlyNames.GetAdminColumnFriendlyName(transaction, "table1", "column1").empty());
            adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                transaction, "table1", "column1", "friendly1");
            EXPECT_EQ(adminColumnFriendlyNames.GetAdminColumnFriendlyName(transaction, "table1", "column1"), "friendly1");
            });
    }

} // namespace {
} // namespace TableHelpers