#include "permission_implications.h"

#include <gtest/gtest.h>

#include "db_schema/permission_implications.h"
#include "permissions.h"
#include "test/src/util/database_test_helper.h"
#include "util/types.h"

namespace TableHelpers {
namespace {

struct TierPerms {
    int64_t silver = 0;
    int64_t silverPartnerAcro = 0;
    int64_t gold = 0;
    int64_t platinum = 0;
    int64_t platinumBenefit = 0;
};

// Seeds the four membership permissions + one orthogonal platinum benefit, and
// wires the nesting silver <- silver_partner_acro <- gold <- platinum plus
// platinum -> benefit.
TierPerms SeedTierHierarchy(Transaction& tx, DatabaseHelper db) {
    Permissions permissions(db);
    TierPerms p;
    p.silver = permissions.AddPermission(tx, "knotty_yoga_silver", "Silver");
    p.silverPartnerAcro = permissions.AddPermission(
        tx, "knotty_yoga_silver_partner_acro", "Silver Partner Acro");
    p.gold = permissions.AddPermission(tx, "knotty_yoga_gold", "Gold");
    p.platinum = permissions.AddPermission(tx, "knotty_yoga_platinum", "Platinum");
    p.platinumBenefit = permissions.AddPermission(tx, "platinum_benefit_x", "Benefit X");

    PermissionImplications impl(db);
    impl.AddImplication(tx, p.silverPartnerAcro, p.silver);
    impl.AddImplication(tx, p.gold, p.silverPartnerAcro);
    impl.AddImplication(tx, p.platinum, p.gold);
    impl.AddImplication(tx, p.platinum, p.platinumBenefit);
    return p;
}

TEST(PermissionImplicationsTest, AddAndGetDirectImplications) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddAndGetDirectImplications", [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        TierPerms p = SeedTierHierarchy(tx, db);
        PermissionImplications impl(db);

        auto platinumDirect = impl.GetDirectImplications(tx, p.platinum);
        EXPECT_EQ(platinumDirect.size(), 2u);  // gold + benefit
        auto goldDirect = impl.GetDirectImplications(tx, p.gold);
        ASSERT_EQ(goldDirect.size(), 1u);
        EXPECT_EQ(goldDirect[0].at(std::string(
                      DbSchema::kPermissionImplicationsImpliesPermissionId)),
                  StringFromInt(p.silverPartnerAcro));
        EXPECT_TRUE(impl.GetDirectImplications(tx, p.silver).empty());
    });
}

TEST(PermissionImplicationsTest, GetAllAndDelete) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetAllAndDelete", [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        Permissions permissions(db);
        int64_t a = permissions.AddPermission(tx, "perm_a", "A");
        int64_t b = permissions.AddPermission(tx, "perm_b", "B");
        PermissionImplications impl(db);
        int64_t edge = impl.AddImplication(tx, a, b);

        EXPECT_EQ(impl.GetAll(tx).size(), 1u);
        impl.DeleteImplication(tx, edge);
        EXPECT_TRUE(impl.GetAll(tx).empty());
    });
}

TEST(PermissionImplicationsTest, ExpandClosureWalksTheChain) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ExpandClosureWalksTheChain", [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        TierPerms p = SeedTierHierarchy(tx, db);
        PermissionImplications impl(db);

        // Platinum closes over gold -> silver_partner_acro -> silver + benefit.
        auto platinum = impl.ExpandClosure(tx, { p.platinum });
        EXPECT_EQ(platinum.count(p.platinum), 1u);
        EXPECT_EQ(platinum.count(p.gold), 1u);
        EXPECT_EQ(platinum.count(p.silverPartnerAcro), 1u);
        EXPECT_EQ(platinum.count(p.silver), 1u);
        EXPECT_EQ(platinum.count(p.platinumBenefit), 1u);
        EXPECT_EQ(platinum.size(), 5u);

        // Gold closes over silver_partner_acro -> silver, but NOT platinum/benefit.
        auto gold = impl.ExpandClosure(tx, { p.gold });
        EXPECT_EQ(gold.count(p.gold), 1u);
        EXPECT_EQ(gold.count(p.silverPartnerAcro), 1u);
        EXPECT_EQ(gold.count(p.silver), 1u);
        EXPECT_EQ(gold.count(p.platinum), 0u);
        EXPECT_EQ(gold.count(p.platinumBenefit), 0u);

        // Silver is a leaf.
        auto silver = impl.ExpandClosure(tx, { p.silver });
        EXPECT_EQ(silver.size(), 1u);
        EXPECT_EQ(silver.count(p.silver), 1u);
    });
}

TEST(PermissionImplicationsTest, ExpandClosureEmptyAndNoEdges) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ExpandClosureEmptyAndNoEdges", [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        Permissions permissions(db);
        int64_t lone = permissions.AddPermission(tx, "lone", "Lone");
        PermissionImplications impl(db);

        EXPECT_TRUE(impl.ExpandClosure(tx, {}).empty());
        auto closure = impl.ExpandClosure(tx, { lone });
        EXPECT_EQ(closure.size(), 1u);
        EXPECT_EQ(closure.count(lone), 1u);
    });
}

TEST(PermissionImplicationsTest, ExpandClosureTerminatesOnCycle) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ExpandClosureTerminatesOnCycle", [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        Permissions permissions(db);
        int64_t a = permissions.AddPermission(tx, "cyc_a", "A");
        int64_t b = permissions.AddPermission(tx, "cyc_b", "B");
        PermissionImplications impl(db);
        impl.AddImplication(tx, a, b);
        impl.AddImplication(tx, b, a);  // cycle

        // UNION (not UNION ALL) dedups, so the recursive CTE terminates.
        auto closure = impl.ExpandClosure(tx, { a });
        EXPECT_EQ(closure.size(), 2u);
        EXPECT_EQ(closure.count(a), 1u);
        EXPECT_EQ(closure.count(b), 1u);
    });
}

}  // namespace
}  // namespace TableHelpers
