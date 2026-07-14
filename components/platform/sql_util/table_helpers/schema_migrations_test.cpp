#include "schema_migrations.h"

#include <stdexcept>

#include <gtest/gtest.h>

#include "db_schema/schema_migrations.h"
#include "test/src/util/database_test_helper.h"

namespace TableHelpers {
namespace {

TEST(SchemaMigrationsTableTest, IsAppliedFalseOnEmptyTable) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsAppliedFalseOnEmptyTable",
        [&](Transaction& transaction) {
        SchemaMigrations helper(testDb.GetDatabaseHelper());
        EXPECT_FALSE(helper.IsApplied(transaction, "0001_baseline"));
        EXPECT_FALSE(helper.IsApplied(transaction, ""));
    });
}

TEST(SchemaMigrationsTableTest, RecordAppliedThenIsAppliedTrue) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RecordAppliedThenIsAppliedTrue",
        [&](Transaction& transaction) {
        SchemaMigrations helper(testDb.GetDatabaseHelper());
        helper.RecordApplied(transaction, "0001_baseline");

        EXPECT_TRUE(helper.IsApplied(transaction, "0001_baseline"));
        // Other ids still report false.
        EXPECT_FALSE(helper.IsApplied(transaction, "0002_subscriptions"));
    });
}

TEST(SchemaMigrationsTableTest, RecordAppliedDuplicateIdThrows) {
    // Primary-key uniqueness is enforced at the DB layer. The helper makes
    // no attempt to swallow that — duplicate IDs are a caller bug. The
    // MigrationRunner relies on `IsApplied` to gate inserts.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RecordAppliedDuplicateIdThrows",
        [&](Transaction& transaction) {
        SchemaMigrations helper(testDb.GetDatabaseHelper());
        helper.RecordApplied(transaction, "0001_baseline");
        EXPECT_THROW(helper.RecordApplied(transaction, "0001_baseline"),
                     std::exception);
    });
}

TEST(SchemaMigrationsTableTest, ListAppliedIdsEmptyOnEmptyTable) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ListAppliedIdsEmptyOnEmptyTable",
        [&](Transaction& transaction) {
        SchemaMigrations helper(testDb.GetDatabaseHelper());
        EXPECT_TRUE(helper.ListAppliedIds(transaction).empty());
    });
}

TEST(SchemaMigrationsTableTest, ListAppliedIdsReturnsInsertionOrder) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ListAppliedIdsReturnsInsertionOrder",
        [&](Transaction& transaction) {
        SchemaMigrations helper(testDb.GetDatabaseHelper());
        helper.RecordApplied(transaction, "0001_baseline");
        helper.RecordApplied(transaction, "0002_subscriptions");
        helper.RecordApplied(transaction, "0003_vouchers");

        StringArray ids = helper.ListAppliedIds(transaction);
        ASSERT_EQ(ids.size(), 3u);
        EXPECT_EQ(ids[0], "0001_baseline");
        EXPECT_EQ(ids[1], "0002_subscriptions");
        EXPECT_EQ(ids[2], "0003_vouchers");
    });
}

TEST(SchemaMigrationsTableTest, ListAppliedIdsTieBreaksOnIdWhenAppliedAtSame) {
    // If two rows somehow land in the same microsecond (now_us() uses
    // clock_timestamp(), so this is very unlikely but possible inside a
    // single transaction with tight back-to-back inserts), the ORDER BY
    // tiebreak on `id` keeps the output deterministic. Verified by
    // forcing the rows to share an applied_at_us via UPDATE.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ListAppliedIdsTieBreaksOnIdWhenAppliedAtSame",
        [&](Transaction& transaction) {
        SchemaMigrations helper(testDb.GetDatabaseHelper());
        helper.RecordApplied(transaction, "0002_subscriptions");
        helper.RecordApplied(transaction, "0001_baseline");
        helper.RecordApplied(transaction, "0003_vouchers");

        // Force all three rows to share the same applied_at_us.
        transaction.RunSqlStatement(
            "UPDATE schema_migrations SET applied_at_us = 1000");

        StringArray ids = helper.ListAppliedIds(transaction);
        ASSERT_EQ(ids.size(), 3u);
        EXPECT_EQ(ids[0], "0001_baseline");
        EXPECT_EQ(ids[1], "0002_subscriptions");
        EXPECT_EQ(ids[2], "0003_vouchers");
    });
}

TEST(SchemaMigrationsTableTest, IsAppliedDistinguishesIds) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsAppliedDistinguishesIds",
        [&](Transaction& transaction) {
        SchemaMigrations helper(testDb.GetDatabaseHelper());
        helper.RecordApplied(transaction, "0001_baseline");
        helper.RecordApplied(transaction, "0003_vouchers");

        EXPECT_TRUE(helper.IsApplied(transaction, "0001_baseline"));
        EXPECT_FALSE(helper.IsApplied(transaction, "0002_subscriptions"));
        EXPECT_TRUE(helper.IsApplied(transaction, "0003_vouchers"));
    });
}

}  // namespace
}  // namespace TableHelpers
