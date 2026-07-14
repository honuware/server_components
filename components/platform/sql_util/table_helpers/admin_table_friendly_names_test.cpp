#include "sql_util/table_helpers/admin_table_friendly_names.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "util/file_util.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"
#include "db_schema/admin_table_friendly_names.h"
#include "sql_util/table_helpers/admin_top_level_tables.h"

namespace TableHelpers {
namespace {

using ::testing::UnorderedElementsAre;
using ::testing::ElementsAre;
using ::testing::Pair;
using ::testing::ContainerEq;

KeyValueTable MakeFriendly(const std::string& id, const std::string& name, const std::string& friendly, const std::string& description) {
    return KeyValueTable{
        std::make_pair((std::string)DbSchema::kAdminTableFriendlyNamesTableFriendlyNameId, id),
        std::make_pair((std::string)DbSchema::kAdminTableFriendlyNamesName, name),
        std::make_pair((std::string)DbSchema::kAdminTableFriendlyNamesFriendlyName, friendly),
        std::make_pair((std::string)DbSchema::kAdminTableFriendlyNamesDescription, description) };
}

KeyValueTable MakeFriendl1(const std::string& id) {
    return MakeFriendly(id, "table1", "friendly1", "description1");
}

KeyValueTable MakeFriendl2(const std::string& id) {
    return MakeFriendly(id, "table2", "friendly2", "description2");
}

std::pair<std::string, std::string> GetTableFriendlyNameIds(
    AdminTableFriendlyNames& names, Transaction& transaction) {
    auto rows = names.GetAdminTableFriendlyNames(transaction);
    std::string id1, id2;
    for (const auto& row : rows) {
        if (row.at(std::string(DbSchema::kAdminTableFriendlyNamesName)) == "table1") {
            id1 = row.at(std::string(DbSchema::kAdminTableFriendlyNamesTableFriendlyNameId));
        } else if (row.at(std::string(DbSchema::kAdminTableFriendlyNamesName)) == "table2") {
            id2 = row.at(std::string(DbSchema::kAdminTableFriendlyNamesTableFriendlyNameId));
        }
    }
    return {id1, id2};
}

TEST(AdminTableFriendlyNamesTest, AddAdminTableFriendlyNameBasic)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("AddAdminTableFriendlyNameBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
        AdminTopLevelTables adminTopLevelTables(databaseHelper);
        adminTopLevelTables.AddAdminTopLevelTable(transaction, "table1");
        adminTopLevelTables.AddAdminTopLevelTable(transaction, "table2");

        AdminTableFriendlyNames adminTableFriendlyNames(databaseHelper);
        adminTableFriendlyNames.AddAdminTableFriendlyName(
            transaction, "table1", "friendly1", "description1");
        adminTableFriendlyNames.AddAdminTableFriendlyName(
            transaction, "table2", "friendly2", "description2");

        auto [id1, id2] = GetTableFriendlyNameIds(adminTableFriendlyNames, transaction);
        EXPECT_THAT(adminTableFriendlyNames.GetAdminTableFriendlyNames(transaction),
            UnorderedElementsAre(
                ContainerEq(MakeFriendl1(id1)),
                ContainerEq(MakeFriendl2(id2))));
        });
}

TEST(AdminTableFriendlyNamesTest, DeleteAdminTableFriendlyNameBasic)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("DeleteAdminTableFriendlyNameBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
        AdminTopLevelTables adminTopLevelTables(databaseHelper);
        adminTopLevelTables.AddAdminTopLevelTable(transaction, "table1");
        adminTopLevelTables.AddAdminTopLevelTable(transaction, "table2");

        AdminTableFriendlyNames adminTableFriendlyNames(databaseHelper);
        adminTableFriendlyNames.AddAdminTableFriendlyName(
            transaction, "table1", "friendly1", "description1");
        adminTableFriendlyNames.AddAdminTableFriendlyName(
            transaction, "table2", "friendly2", "description2");

        auto [id1, id2] = GetTableFriendlyNameIds(adminTableFriendlyNames, transaction);
        EXPECT_THAT(adminTableFriendlyNames.GetAdminTableFriendlyNames(transaction),
            UnorderedElementsAre(
                ContainerEq(MakeFriendl1(id1)),
                ContainerEq(MakeFriendl2(id2))));
        adminTableFriendlyNames.DeleteAdminTableFriendlyName(transaction, "table1");
        EXPECT_THAT(adminTableFriendlyNames.GetAdminTableFriendlyNames(transaction),
            UnorderedElementsAre(
                ContainerEq(MakeFriendl2(id2))));
        adminTableFriendlyNames.DeleteAdminTableFriendlyName(transaction, "table2");
        EXPECT_THAT(adminTableFriendlyNames.GetAdminTableFriendlyNames(transaction), UnorderedElementsAre());
        });
}

TEST(AdminTableFriendlyNamesTest, GetAdminTableFriendlyNameBasic)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("GetAdminTableFriendlyNameBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
        AdminTopLevelTables adminTopLevelTables(databaseHelper);
        adminTopLevelTables.AddAdminTopLevelTable(transaction, "table1");
        adminTopLevelTables.AddAdminTopLevelTable(transaction, "table2");

        AdminTableFriendlyNames adminTableFriendlyNames(databaseHelper);
        KeyValueTable keyValueTable;
        ASSERT_FALSE(adminTableFriendlyNames.GetAdminTableFriendlyName(transaction, "table1", keyValueTable));
        adminTableFriendlyNames.AddAdminTableFriendlyName(
            transaction, "table1", "friendly1", "description1");
        ASSERT_TRUE(adminTableFriendlyNames.GetAdminTableFriendlyName(transaction, "table1", keyValueTable));
        std::string actualId = keyValueTable.at(std::string(DbSchema::kAdminTableFriendlyNamesTableFriendlyNameId));
        EXPECT_THAT(keyValueTable, ContainerEq(MakeFriendl1(actualId)));
        });
}

} // namespace {
} // namespace TableHelpers