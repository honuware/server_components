#include "sql_util/table_helpers/admin_column_redactions.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "db_schema/admin_column_redactions.h"
#include "db_schema/people.h"
#include "db_schema/sessions.h"
#include "db_schema/device_tokens.h"
#include "db_schema/email_verifications.h"
#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"

namespace TableHelpers {
namespace {

using ::testing::UnorderedElementsAre;

// Build a (table, column) pair the same way LoadColumnRedactionSet does.
std::pair<std::string, std::string> Redaction(
    std::string_view tableName, std::string_view columnName) {
    return { std::string(tableName), std::string(columnName) };
}

TEST(AdminColumnRedactionsTest, AddAndListBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("AddAndListBasic", [&](Transaction& transaction) {
        DatabaseHelper db = testDatabaseUtil.GetDatabaseHelper();
        AdminColumnRedactions helper(db);

        helper.AddAdminColumnRedaction(transaction, "table_a", "secret");
        helper.AddAdminColumnRedaction(transaction, "table_b", "token");

        auto rows = helper.GetAdminColumnRedactions(transaction);
        ASSERT_EQ(rows.size(), 2u);

        ColumnRedactionSet set = helper.LoadColumnRedactionSet(transaction);
        EXPECT_THAT(set, UnorderedElementsAre(
            Redaction("table_a", "secret"),
            Redaction("table_b", "token")));
    });
}

TEST(AdminColumnRedactionsTest, IsRedactedReturnsTrueOnlyForExactPair) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction(
        "IsRedactedReturnsTrueOnlyForExactPair",
        [&](Transaction& transaction) {
            DatabaseHelper db = testDatabaseUtil.GetDatabaseHelper();
            AdminColumnRedactions helper(db);

            helper.AddAdminColumnRedaction(transaction, "people", "password_hash");

            EXPECT_TRUE(helper.IsRedacted(transaction, "people", "password_hash"));
            // Other column on the same table is NOT redacted.
            EXPECT_FALSE(helper.IsRedacted(transaction, "people", "email"));
            // Same column name on a different table is NOT redacted.
            EXPECT_FALSE(helper.IsRedacted(transaction, "other_table", "password_hash"));
        });
}

TEST(AdminColumnRedactionsTest, DeleteRemovesEntry) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("DeleteRemovesEntry", [&](Transaction& transaction) {
        DatabaseHelper db = testDatabaseUtil.GetDatabaseHelper();
        AdminColumnRedactions helper(db);

        helper.AddAdminColumnRedaction(transaction, "t1", "c1");
        helper.AddAdminColumnRedaction(transaction, "t2", "c2");
        ASSERT_TRUE(helper.IsRedacted(transaction, "t1", "c1"));

        helper.DeleteAdminColumnRedaction(transaction, "t1", "c1");

        EXPECT_FALSE(helper.IsRedacted(transaction, "t1", "c1"));
        // The other entry survives.
        EXPECT_TRUE(helper.IsRedacted(transaction, "t2", "c2"));
    });
}

TEST(AdminColumnRedactionsTest, LoadColumnRedactionSetEmptyByDefault) {
    // The table is created at test-suite startup but populated only by
    // CreateAndPopulateDatabases. In a fresh per-test transaction
    // before we add anything, the set must be empty so the JSON
    // boundary doesn't fall back to redacting nothing-or-everything.
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction(
        "LoadColumnRedactionSetEmptyByDefault",
        [&](Transaction& transaction) {
            DatabaseHelper db = testDatabaseUtil.GetDatabaseHelper();
            AdminColumnRedactions helper(db);

            ColumnRedactionSet set = helper.LoadColumnRedactionSet(transaction);
            EXPECT_TRUE(set.empty());
        });
}

// Phase 3.8: the canonical seed list is set in stone for the security
// review. If the population helper drifts (or someone deletes one of
// the entries), this test fails so the operator notices before it
// ships.
TEST(AdminColumnRedactionsTest, RedactionMapContainsExpectedDefaults) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction(
        "RedactionMapContainsExpectedDefaults",
        [&](Transaction& transaction) {
            DatabaseHelper db = testDatabaseUtil.GetDatabaseHelper();
            AdminColumnRedactions helper(db);

            // Mirror the canonical seed list defined in
            // create_database.cpp::PopulateAdminColumnRedactions. The
            // production database loads these via PopulateTables; the
            // test setup does NOT auto-populate them in every
            // transaction, so we assert the set semantics directly.
            helper.AddAdminColumnRedaction(
                transaction, DbSchema::kPeopleTable, DbSchema::kPeoplePasswordHash);
            helper.AddAdminColumnRedaction(
                transaction, DbSchema::kDeviceTokensTable, DbSchema::kDeviceTokensSecretHash);
            helper.AddAdminColumnRedaction(
                transaction, DbSchema::kEmailVerificationsTable,
                DbSchema::kEmailVerificationsTokenHash);
            helper.AddAdminColumnRedaction(
                transaction, DbSchema::kSessionsTable, DbSchema::kSessionsUuid);

            ColumnRedactionSet set = helper.LoadColumnRedactionSet(transaction);
            EXPECT_THAT(set, UnorderedElementsAre(
                Redaction(DbSchema::kPeopleTable, DbSchema::kPeoplePasswordHash),
                Redaction(DbSchema::kDeviceTokensTable, DbSchema::kDeviceTokensSecretHash),
                Redaction(
                    DbSchema::kEmailVerificationsTable,
                    DbSchema::kEmailVerificationsTokenHash),
                Redaction(DbSchema::kSessionsTable, DbSchema::kSessionsUuid)));
        });
}

}  // namespace
}  // namespace TableHelpers
