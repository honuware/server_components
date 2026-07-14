#include "role_permissions.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "db_schema/roles.h"
#include "db_schema/permissions.h"
#include "db_schema/role_permissions.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "test/src/util/database_test_helper.h"
#include "util/types.h"

namespace TableHelpers {
namespace {

static int64_t AddRole(Transaction& transaction, TestDatabaseUtil& testDb, std::string_view name, std::string_view description) {
    KeyValueTable kv = {
        { std::string(DbSchema::kRolesName), std::string(name) },
        { std::string(DbSchema::kRolesDescription), std::string(description) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        testDb.GetDatabaseHelper(),
        DbSchema::kRolesTable,
        kv);
}

static int64_t AddPermission(Transaction& transaction, TestDatabaseUtil& testDb, std::string_view name, std::string_view description) {
    KeyValueTable kv = {
        { std::string(DbSchema::kPermissionsName), std::string(name) },
        { std::string(DbSchema::kPermissionsDescription), std::string(description) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        testDb.GetDatabaseHelper(),
        DbSchema::kPermissionsTable,
        kv);
}

TEST(RolePermissionsTest, AddRolePermissionBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddRolePermissionBasic", [&](Transaction& transaction) {

        RolePermissions rp(testDb.GetDatabaseHelper());

        int64_t roleId = AddRole(transaction, testDb, "admin", "Administrator");
        int64_t permId = AddPermission(transaction, testDb, "read", "Read access");

        int64_t id = rp.AddRolePermission(transaction, roleId, permId);
        ASSERT_GT(id, 0);

        KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM role_permissions WHERE id = $1", StringFromInt(id));
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(row.at(std::string(DbSchema::kRolePermissionsRoleId)), StringFromInt(roleId));
        EXPECT_EQ(row.at(std::string(DbSchema::kRolePermissionsPermissionId)), StringFromInt(permId));
    });
}

TEST(RolePermissionsTest, AddRolePermissionDuplicate) {
    // Phase 1.5 of the security review: (role_id, permission_id) is unique.
    // Inserting the same pair twice must fail with a Postgres unique
    // violation, surfaced as an exception.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddRolePermissionDuplicate", [&](Transaction& transaction) {

        RolePermissions rp(testDb.GetDatabaseHelper());

        int64_t roleId = AddRole(transaction, testDb, "dup-role", "Dup role");
        int64_t permId = AddPermission(transaction, testDb, "dup-perm", "Dup perm");

        ASSERT_GT(rp.AddRolePermission(transaction, roleId, permId), 0);
        EXPECT_THROW(
            rp.AddRolePermission(transaction, roleId, permId),
            std::exception);
    });
}

TEST(RolePermissionsTest, GetRolePermissionBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolePermissionBasic", [&](Transaction& transaction) {

        RolePermissions rp(testDb.GetDatabaseHelper());

        int64_t roleId = AddRole(transaction, testDb, "user", "Standard user");
        int64_t permId = AddPermission(transaction, testDb, "write", "Write access");

        int64_t id = rp.AddRolePermission(transaction, roleId, permId);
        KeyValueTable row = rp.GetRolePermission(transaction, id);
        EXPECT_EQ(row.at(std::string(DbSchema::kRolePermissionsRoleId)), StringFromInt(roleId));
        EXPECT_EQ(row.at(std::string(DbSchema::kRolePermissionsPermissionId)), StringFromInt(permId));
    });
}

TEST(RolePermissionsTest, GetRolePersmissionNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolePersmissionNotFound", [&](Transaction& transaction) {

        RolePermissions rp(testDb.GetDatabaseHelper());

        EXPECT_THROW(rp.GetRolePermission(transaction, 424242), std::runtime_error);
    });
}

TEST(RolePermissionsTest, GetRolePermissionsForRoleBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolePermissionsForRoleBasic", [&](Transaction& transaction) {

        RolePermissions rp(testDb.GetDatabaseHelper());

        int64_t roleId = AddRole(transaction, testDb, "auditor", "Auditor");
        int64_t permId1 = AddPermission(transaction, testDb, "read", "Read");
        int64_t permId2 = AddPermission(transaction, testDb, "download", "Download");
        rp.AddRolePermission(transaction, roleId, permId1);
        rp.AddRolePermission(transaction, roleId, permId2);

        KeyValueTableArray arr = rp.GetRolePermissionsForRole(transaction, roleId);
        ASSERT_EQ(arr.size(), 2u);
        EXPECT_EQ(arr[0].at(std::string(DbSchema::kRolePermissionsRoleId)), StringFromInt(roleId));
        EXPECT_EQ(arr[1].at(std::string(DbSchema::kRolePermissionsRoleId)), StringFromInt(roleId));
    });
}

TEST(RolePermissionsTest, GetRolePermissionForRoleNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolePermissionForRoleNotFound", [&](Transaction& transaction) {

        RolePermissions rp(testDb.GetDatabaseHelper());

        KeyValueTableArray arr = rp.GetRolePermissionsForRole(transaction, 999999);
        EXPECT_TRUE(arr.empty());
    });
}

TEST(RolePermissionsTest, GetRolePermissionForRoleNoRolePermissions) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolePermissionForRoleNoRolePermissions", [&](Transaction& transaction) {

        RolePermissions rp(testDb.GetDatabaseHelper());

        // Role exists but has no permissions assigned.
        int64_t roleId = AddRole(transaction, testDb, "unassigned", "Unassigned role");
        (void)AddPermission(transaction, testDb, "read", "Read");

        KeyValueTableArray arr = rp.GetRolePermissionsForRole(transaction, roleId);
        EXPECT_TRUE(arr.empty());
    });
}

TEST(RolePermissionsTest, GetRolePermissionsForPermissionBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolePermissionsForPermissionBasic", [&](Transaction& transaction) {

        RolePermissions rp(testDb.GetDatabaseHelper());

        int64_t roleId1 = AddRole(transaction, testDb, "viewer", "Viewer");
        int64_t roleId2 = AddRole(transaction, testDb, "editor", "Editor");
        int64_t permId = AddPermission(transaction, testDb, "read", "Read");
        rp.AddRolePermission(transaction, roleId1, permId);
        rp.AddRolePermission(transaction, roleId2, permId);

        KeyValueTableArray arr = rp.GetRolePermissionsForPermission(transaction, permId);
        ASSERT_EQ(arr.size(), 2u);
        EXPECT_EQ(arr[0].at(std::string(DbSchema::kRolePermissionsPermissionId)), StringFromInt(permId));
        EXPECT_EQ(arr[1].at(std::string(DbSchema::kRolePermissionsPermissionId)), StringFromInt(permId));
    });
}

TEST(RolePermissionsTest, GetRolePermissionForPermissionNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolePermissionForPermissionNotFound", [&](Transaction& transaction) {

        RolePermissions rp(testDb.GetDatabaseHelper());

        KeyValueTableArray arr = rp.GetRolePermissionsForPermission(transaction, 999999);
        EXPECT_TRUE(arr.empty());
    });
}

TEST(RolePermissionsTest, GetRolePermissionForPermissionNoPermissions) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolePermissionForPermissionNoPermissions", [&](Transaction& transaction) {

        RolePermissions rp(testDb.GetDatabaseHelper());

        // Permission exists but is not assigned to any role.
        (void)AddRole(transaction, testDb, "viewer", "Viewer");
        int64_t permId = AddPermission(transaction, testDb, "orphan", "Orphan permission");

        KeyValueTableArray arr = rp.GetRolePermissionsForPermission(transaction, permId);
        EXPECT_TRUE(arr.empty());
    });
}

TEST(RolePermissionsTest, GetRolePermissionsBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolePermissionsBasic", [&](Transaction& transaction) {

        RolePermissions rp(testDb.GetDatabaseHelper());

        int64_t roleId = AddRole(transaction, testDb, "ops", "Operations");
        int64_t permId = AddPermission(transaction, testDb, "restart", "Restart service");
        rp.AddRolePermission(transaction, roleId, permId);

        KeyValueTableArray arr = rp.GetRolePermissions(transaction);
        ASSERT_EQ(arr.size(), 1u);
        EXPECT_EQ(arr[0].at(std::string(DbSchema::kRolePermissionsRoleId)), StringFromInt(roleId));
        EXPECT_EQ(arr[0].at(std::string(DbSchema::kRolePermissionsPermissionId)), StringFromInt(permId));
    });
}

TEST(RolePermissionsTest, GetRolePersmissionsNoRolePermissions) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolePersmissionsNoRolePermissions", [&](Transaction& transaction) {

        RolePermissions rp(testDb.GetDatabaseHelper());

        KeyValueTableArray arr = rp.GetRolePermissions(transaction);
        EXPECT_TRUE(arr.empty());
    });
}

TEST(RolePermissionsTest, DeleteRolePermissionBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteRolePermissionBasic", [&](Transaction& transaction) {

        RolePermissions rp(testDb.GetDatabaseHelper());

        int64_t roleId = AddRole(transaction, testDb, "temp", "Temp role");
        int64_t permId = AddPermission(transaction, testDb, "tempperm", "Temp perm");
        int64_t id = rp.AddRolePermission(transaction, roleId, permId);

        // ensure exists
        KeyValueTable pre = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM role_permissions WHERE id = $1", StringFromInt(id));
        ASSERT_FALSE(pre.empty());

        rp.DeleteRolePermission(transaction, id);

        std::string countStr = transaction.RunSqlStatementReturningOneValue(
            "SELECT COUNT(*) FROM role_permissions WHERE id = $1", StringFromInt(id));
        EXPECT_EQ(std::stoi(countStr), 0);
    });
}

TEST(RolePermissionsTest, DeleteRolePermissionNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteRolePermissionNotFound", [&](Transaction& transaction) {

        RolePermissions rp(testDb.GetDatabaseHelper());

        // Should not throw
        EXPECT_NO_THROW(rp.DeleteRolePermission(transaction, 424242));
    });
}

} // namespace
} // namespace TableHelpers