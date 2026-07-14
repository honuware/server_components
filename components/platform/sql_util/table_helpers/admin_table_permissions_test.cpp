#include "sql_util/table_helpers/admin_table_permissions.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "sql_util/table_helpers/permissions.h"
#include "db_schema/admin_table_permissions.h"
#include "db_schema/permissions.h"
#include "test/src/util/database_test_helper.h"

namespace TableHelpers {
namespace {

using ::testing::UnorderedElementsAre;

int64_t CreatePermission(
    Transaction& transaction,
    DatabaseHelper databaseHelper,
    std::string_view name) {
    Permissions permissions(databaseHelper);
    return permissions.AddPermission(transaction, name, "test permission");
}

TEST(AdminTablePermissionsTest, AddAndGetPermissionsForTable) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddAndGetPermissionsForTable", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        int64_t permId = CreatePermission(transaction, databaseHelper, "manage_products");

        AdminTablePermissions atp(databaseHelper);
        atp.AddAdminTablePermission(transaction, "products", permId);
        atp.AddAdminTablePermission(transaction, "product_prices", permId);

        auto productsPerms = atp.GetPermissionsForTable(transaction, "products");
        ASSERT_EQ(productsPerms.size(), 1u);
        EXPECT_EQ(productsPerms[0].at(std::string(DbSchema::kAdminTablePermissionsTableName)), "products");

        auto pricesPerms = atp.GetPermissionsForTable(transaction, "product_prices");
        ASSERT_EQ(pricesPerms.size(), 1u);
        EXPECT_EQ(pricesPerms[0].at(std::string(DbSchema::kAdminTablePermissionsTableName)), "product_prices");

        auto emptyPerms = atp.GetPermissionsForTable(transaction, "nonexistent");
        EXPECT_EQ(emptyPerms.size(), 0u);
    });
}

TEST(AdminTablePermissionsTest, GetTablesForPermissionId) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetTablesForPermissionId", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        int64_t permId1 = CreatePermission(transaction, databaseHelper, "manage_products");
        int64_t permId2 = CreatePermission(transaction, databaseHelper, "manage_users");

        AdminTablePermissions atp(databaseHelper);
        atp.AddAdminTablePermission(transaction, "products", permId1);
        atp.AddAdminTablePermission(transaction, "product_prices", permId1);
        atp.AddAdminTablePermission(transaction, "people", permId2);

        StringArray tablesForPerm1 = atp.GetTablesForPermissionId(transaction, permId1);
        EXPECT_THAT(tablesForPerm1, UnorderedElementsAre("products", "product_prices"));

        StringArray tablesForPerm2 = atp.GetTablesForPermissionId(transaction, permId2);
        EXPECT_THAT(tablesForPerm2, UnorderedElementsAre("people"));

        StringArray tablesForNone = atp.GetTablesForPermissionId(transaction, 99999);
        EXPECT_TRUE(tablesForNone.empty());
    });
}

TEST(AdminTablePermissionsTest, DeleteAdminTablePermission) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteAdminTablePermission", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        int64_t permId = CreatePermission(transaction, databaseHelper, "manage_products");

        AdminTablePermissions atp(databaseHelper);
        int64_t id1 = atp.AddAdminTablePermission(transaction, "products", permId);
        int64_t id2 = atp.AddAdminTablePermission(transaction, "product_prices", permId);

        StringArray tables = atp.GetTablesForPermissionId(transaction, permId);
        EXPECT_EQ(tables.size(), 2u);

        atp.DeleteAdminTablePermission(transaction, id1);
        tables = atp.GetTablesForPermissionId(transaction, permId);
        EXPECT_THAT(tables, UnorderedElementsAre("product_prices"));

        atp.DeleteAdminTablePermission(transaction, id2);
        tables = atp.GetTablesForPermissionId(transaction, permId);
        EXPECT_TRUE(tables.empty());
    });
}

TEST(AdminTablePermissionsTest, GetAllAdminTablePermissions) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetAllAdminTablePermissions", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        int64_t permId1 = CreatePermission(transaction, databaseHelper, "manage_products");
        int64_t permId2 = CreatePermission(transaction, databaseHelper, "manage_users");

        AdminTablePermissions atp(databaseHelper);
        atp.AddAdminTablePermission(transaction, "products", permId1);
        atp.AddAdminTablePermission(transaction, "people", permId2);

        KeyValueTableArray all = atp.GetAllAdminTablePermissions(transaction);
        EXPECT_GE(all.size(), 2u);
    });
}

TEST(AdminTablePermissionsTest, MultiplePermissionsForSameTable) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("MultiplePermissionsForSameTable", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        int64_t permId1 = CreatePermission(transaction, databaseHelper, "manage_products");
        int64_t permId2 = CreatePermission(transaction, databaseHelper, "manage_events");

        AdminTablePermissions atp(databaseHelper);
        atp.AddAdminTablePermission(transaction, "event_sessions", permId1);
        atp.AddAdminTablePermission(transaction, "event_sessions", permId2);

        auto perms = atp.GetPermissionsForTable(transaction, "event_sessions");
        EXPECT_EQ(perms.size(), 2u);

        // Both permissions should grant access to event_sessions
        StringArray tables1 = atp.GetTablesForPermissionId(transaction, permId1);
        EXPECT_THAT(tables1, UnorderedElementsAre("event_sessions"));

        StringArray tables2 = atp.GetTablesForPermissionId(transaction, permId2);
        EXPECT_THAT(tables2, UnorderedElementsAre("event_sessions"));
    });
}

} // namespace
} // namespace TableHelpers
