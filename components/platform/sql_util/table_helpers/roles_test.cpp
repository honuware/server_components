#include "roles.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "db_schema/roles.h"
#include "sql_util/database_access/database_helper.h"
#include "test/src/util/database_test_helper.h"
#include "util/types.h"

namespace TableHelpers {
namespace {

TEST(RolesTest, AddRoleBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddRoleBasic", [&](Transaction& transaction) {

        Roles roles(testDb.GetDatabaseHelper());

        int64_t id = roles.AddRole(transaction, "admin", "Administrator role");
        ASSERT_GT(id, 0);

        KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM roles WHERE id = $1", StringFromInt(id));
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(row.at(std::string(DbSchema::kRolesName)), "admin");
        EXPECT_EQ(row.at(std::string(DbSchema::kRolesDescription)), "Administrator role");
    });
}

TEST(RolesTest, GetRoleIdBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRoleIdBasic", [&](Transaction& transaction) {

        Roles roles(testDb.GetDatabaseHelper());

        int64_t id = roles.AddRole(transaction, "user", "Standard user");
        KeyValueTable row = roles.GetRole(transaction, id);
        EXPECT_EQ(row.at(std::string(DbSchema::kRolesName)), "user");
        EXPECT_EQ(row.at(std::string(DbSchema::kRolesDescription)), "Standard user");
    });
}

TEST(RolesTest, GetRoleNameBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRoleNameBasic", [&](Transaction& transaction) {

        Roles roles(testDb.GetDatabaseHelper());

        int64_t id = roles.AddRole(transaction, "auditor", "Audits things");
        (void)id;
        KeyValueTable row = roles.GetRole(transaction, "auditor");
        EXPECT_EQ(row.at(std::string(DbSchema::kRolesName)), "auditor");
        EXPECT_EQ(row.at(std::string(DbSchema::kRolesDescription)), "Audits things");
    });
}

TEST(RolesTest, GetRoleIdNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRoleIdNotFound", [&](Transaction& transaction) {

        Roles roles(testDb.GetDatabaseHelper());

        EXPECT_THROW(roles.GetRole(transaction, 424242), std::runtime_error);
    });
}

TEST(RolesTest, GetRoleNameNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRoleNameNotFound", [&](Transaction& transaction) {

        Roles roles(testDb.GetDatabaseHelper());

        EXPECT_THROW(roles.GetRole(transaction, "missing"), std::runtime_error);
    });
}

TEST(RolesTest, GetRolesBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolesBasic", [&](Transaction& transaction) {

        Roles roles(testDb.GetDatabaseHelper());

        int64_t id1 = roles.AddRole(transaction, "admin", "Administrator role");
        int64_t id2 = roles.AddRole(transaction, "user", "Standard user");
        ASSERT_GT(id1, 0);
        ASSERT_GT(id2, 0);

        KeyValueTableArray arr = roles.GetRoles(transaction);
        ASSERT_EQ(arr.size(), 2u);
        EXPECT_EQ(arr[0].at(std::string(DbSchema::kRolesName)), "admin");
        EXPECT_EQ(arr[1].at(std::string(DbSchema::kRolesName)), "user");
    });
}

TEST(RolesTest, GetRolesNoRoles) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolesNoRoles", [&](Transaction& transaction) {

        Roles roles(testDb.GetDatabaseHelper());

        KeyValueTableArray arr = roles.GetRoles(transaction);
        EXPECT_TRUE(arr.empty());
    });
}

TEST(RolesTest, SetNameBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetNameBasic", [&](Transaction& transaction) {

        Roles roles(testDb.GetDatabaseHelper());

        int64_t id = roles.AddRole(transaction, "legacy", "Legacy role");
        roles.SetName(transaction, id, "modern");

        KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM roles WHERE id = $1", StringFromInt(id));
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(row.at(std::string(DbSchema::kRolesName)), "modern");
        EXPECT_EQ(row.at(std::string(DbSchema::kRolesDescription)), "Legacy role");
    });
}

TEST(RolesTest, SetNameNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetNameNotFound", [&](Transaction& transaction) {

        Roles roles(testDb.GetDatabaseHelper());

        EXPECT_THROW(roles.SetName(transaction, 9999, "x"), std::runtime_error);
    });
}

TEST(RolesTest, SetDescriptionBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetDescriptionBasic", [&](Transaction& transaction) {

        Roles roles(testDb.GetDatabaseHelper());

        int64_t id = roles.AddRole(transaction, "ops", "Operations");
        roles.SetDescription(transaction, id, "Ops and maintenance");

        KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM roles WHERE id = $1", StringFromInt(id));
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(row.at(std::string(DbSchema::kRolesName)), "ops");
        EXPECT_EQ(row.at(std::string(DbSchema::kRolesDescription)), "Ops and maintenance");
    });
}

TEST(RolesTest, SetDescriptionNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetDescriptionNotFound", [&](Transaction& transaction) {

        Roles roles(testDb.GetDatabaseHelper());

        EXPECT_THROW(roles.SetDescription(transaction, 9999, "x"), std::runtime_error);
    });
}

TEST(RolesTest, DeleteRoleBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteRoleBasic", [&](Transaction& transaction) {

        Roles roles(testDb.GetDatabaseHelper());

        int64_t id = roles.AddRole(transaction, "temp", "To be deleted");
        // pre-check
        KeyValueTable pre = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM roles WHERE id = $1", StringFromInt(id));
        ASSERT_FALSE(pre.empty());

        roles.DeleteRole(transaction, id);

        std::string countStr = transaction.RunSqlStatementReturningOneValue(
            "SELECT COUNT(*) FROM roles WHERE id = $1", StringFromInt(id));
        EXPECT_EQ(std::stoi(countStr), 0);
    });
}

TEST(RolesTest, DeleteRoleNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteRoleNotFound", [&](Transaction& transaction) {

        Roles roles(testDb.GetDatabaseHelper());

        // Should not throw
        EXPECT_NO_THROW(roles.DeleteRole(transaction, 424242));
    });
}

} // namespace
} // namespace TableHelpers