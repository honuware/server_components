#include "permissions.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "db_schema/permissions.h"
#include "test/src/util/database_test_helper.h"
#include "util/types.h"

namespace TableHelpers {
namespace {

TEST(PermissionsTest, AddPermissionBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddPermissionBasic", [&](Transaction& transaction) {

        Permissions perms(testDb.GetDatabaseHelper());

        int64_t id = perms.AddPermission(transaction, "read", "Read access");
        ASSERT_GT(id, 0);

        KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM permissions WHERE id = $1", StringFromInt(id));
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(row.at(std::string(DbSchema::kPermissionsName)), "read");
        EXPECT_EQ(row.at(std::string(DbSchema::kPermissionsDescription)), "Read access");
    });
}

TEST(PermissionsTest, GetPermissionIdBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetPermissionIdBasic", [&](Transaction& transaction) {

        Permissions perms(testDb.GetDatabaseHelper());

        int64_t id = perms.AddPermission(transaction, "write", "Write access");
        KeyValueTable row = perms.GetPermission(transaction, id);
        EXPECT_EQ(row.at(std::string(DbSchema::kPermissionsName)), "write");
        EXPECT_EQ(row.at(std::string(DbSchema::kPermissionsDescription)), "Write access");
    });
}

TEST(PermissionsTest, GetPermissionNameBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetPermissionNameBasic", [&](Transaction& transaction) {

        Permissions perms(testDb.GetDatabaseHelper());

        int64_t id = perms.AddPermission(transaction, "delete", "Delete access");
        (void)id;
        KeyValueTable row = perms.GetPermission(transaction, "delete");
        EXPECT_EQ(row.at(std::string(DbSchema::kPermissionsName)), "delete");
        EXPECT_EQ(row.at(std::string(DbSchema::kPermissionsDescription)), "Delete access");
    });
}

TEST(PermissionsTest, GetPersmissionIdNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetPersmissionIdNotFound", [&](Transaction& transaction) {

        Permissions perms(testDb.GetDatabaseHelper());

        EXPECT_THROW(perms.GetPermission(transaction, 424242), std::runtime_error);
    });
}

TEST(PermissionsTest, GetPersmissionNameNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetPersmissionNameNotFound", [&](Transaction& transaction) {

        Permissions perms(testDb.GetDatabaseHelper());

        EXPECT_THROW(perms.GetPermission(transaction, "missing"), std::runtime_error);
    });
}

TEST(PermissionsTest, GetPermissionsBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetPermissionsBasic", [&](Transaction& transaction) {

        Permissions perms(testDb.GetDatabaseHelper());

        int64_t id1 = perms.AddPermission(transaction, "read", "Read access");
        int64_t id2 = perms.AddPermission(transaction, "write", "Write access");
        ASSERT_GT(id1, 0);
        ASSERT_GT(id2, 0);

        KeyValueTableArray arr = perms.GetPermissions(transaction);
        ASSERT_EQ(arr.size(), 2u);
        EXPECT_EQ(arr[0].at(std::string(DbSchema::kPermissionsName)), "read");
        EXPECT_EQ(arr[1].at(std::string(DbSchema::kPermissionsName)), "write");
    });
}

TEST(PermissionsTest, GetPersmissionsNoPermissions) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetPersmissionsNoPermissions", [&](Transaction& transaction) {

        Permissions perms(testDb.GetDatabaseHelper());

        KeyValueTableArray arr = perms.GetPermissions(transaction);
        EXPECT_TRUE(arr.empty());
    });
}

TEST(PermissionsTest, SetNameBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetNameBasic", [&](Transaction& transaction) {

        Permissions perms(testDb.GetDatabaseHelper());

        int64_t id = perms.AddPermission(transaction, "legacy", "Legacy permission");
        perms.SetName(transaction, id, "modern");

        KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM permissions WHERE id = $1", StringFromInt(id));
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(row.at(std::string(DbSchema::kPermissionsName)), "modern");
        EXPECT_EQ(row.at(std::string(DbSchema::kPermissionsDescription)), "Legacy permission");
    });
}

TEST(PermissionsTest, SetNameNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetNameNotFound", [&](Transaction& transaction) {

        Permissions perms(testDb.GetDatabaseHelper());

        EXPECT_THROW(perms.SetName(transaction, 9999, "x"), std::runtime_error);
    });
}

TEST(PermissionsTest, SetDescriptionBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetDescriptionBasic", [&](Transaction& transaction) {

        Permissions perms(testDb.GetDatabaseHelper());

        int64_t id = perms.AddPermission(transaction, "ops", "Operations");
        perms.SetDescription(transaction, id, "Ops extended");

        KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM permissions WHERE id = $1", StringFromInt(id));
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(row.at(std::string(DbSchema::kPermissionsName)), "ops");
        EXPECT_EQ(row.at(std::string(DbSchema::kPermissionsDescription)), "Ops extended");
    });
}

TEST(PermissionsTest, SetDescriptionNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetDescriptionNotFound", [&](Transaction& transaction) {

        Permissions perms(testDb.GetDatabaseHelper());

        EXPECT_THROW(perms.SetDescription(transaction, 9999, "x"), std::runtime_error);
    });
}

TEST(PermissionsTest, DeletePermissionBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeletePermissionBasic", [&](Transaction& transaction) {

        Permissions perms(testDb.GetDatabaseHelper());

        int64_t id = perms.AddPermission(transaction, "temp", "To be deleted");
        // pre-check
        KeyValueTable pre = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM permissions WHERE id = $1", StringFromInt(id));
        ASSERT_FALSE(pre.empty());

        perms.DeletePermission(transaction, id);

        std::string countStr = transaction.RunSqlStatementReturningOneValue(
            "SELECT COUNT(*) FROM permissions WHERE id = $1", StringFromInt(id));
        EXPECT_EQ(std::stoi(countStr), 0);
    });
}

TEST(PermissionsTest, DeletePermissionNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeletePermissionNotFound", [&](Transaction& transaction) {

        Permissions perms(testDb.GetDatabaseHelper());

        // Should not throw
        EXPECT_NO_THROW(perms.DeletePermission(transaction, 424242));
    });
}

// §10.2 — pricing-eligible flag defaults off, toggles, and filters the set.
TEST(PermissionsTest, PricingEligibleDefaultsFalseAndToggles) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("PricingEligibleToggles", [&](Transaction& transaction) {
        Permissions perms(testDb.GetDatabaseHelper());

        int64_t goldId = perms.AddPermission(transaction, "tier_gold", "Gold");
        int64_t staffId = perms.AddPermission(transaction, "staff_only", "Staff");

        auto containsId = [](const KeyValueTableArray& rows, int64_t id) {
            for (const auto& row : rows) {
                auto it = row.find(std::string(DbSchema::kPermissionsId));
                if (it != row.end() && it->second == StringFromInt(id)) return true;
            }
            return false;
        };

        // Defaults to not-eligible.
        EXPECT_FALSE(containsId(
            perms.GetPricingEligiblePermissions(transaction), goldId));

        perms.SetPricingEligible(transaction, goldId, true);
        KeyValueTableArray eligible = perms.GetPricingEligiblePermissions(transaction);
        EXPECT_TRUE(containsId(eligible, goldId));
        EXPECT_FALSE(containsId(eligible, staffId));

        KeyValueTable row = perms.GetPermission(transaction, goldId);
        EXPECT_EQ(row.at(std::string(DbSchema::kPermissionsIsPricingEligible)), "t");

        perms.SetPricingEligible(transaction, goldId, false);
        EXPECT_FALSE(containsId(
            perms.GetPricingEligiblePermissions(transaction), goldId));
    });
}

TEST(PermissionsTest, SetPricingEligibleNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetPricingEligibleNotFound", [&](Transaction& transaction) {
        Permissions perms(testDb.GetDatabaseHelper());
        EXPECT_THROW(perms.SetPricingEligible(transaction, 9999, true),
                     std::runtime_error);
    });
}

} // namespace
} // namespace TableHelpers