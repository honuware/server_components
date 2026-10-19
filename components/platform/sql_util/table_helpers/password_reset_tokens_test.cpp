#include "sql_util/table_helpers/password_reset_tokens.h"

#include <gtest/gtest.h>

#include "db_schema/password_reset_tokens.h"
#include "sql_util/table_helpers/people.h"
#include "test/src/util/database_test_helper.h"

namespace TableHelpers {
namespace {

constexpr int64_t kHourUs = 60LL * 60LL * 1000000LL;

int64_t AddTestPerson(
    Transaction& transaction, TestDatabaseUtil& testDb, std::string_view email) {
    People people(testDb.GetDatabaseHelper());
    return people.AddPerson(transaction, email, "First", "Last", "hash");
}

int64_t NowUs(Transaction& transaction) {
    return std::stoll(transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));
}

std::string Column(const KeyValueTable& row, std::string_view column) {
    return row.at(std::string(column));
}

TEST(PasswordResetTokensTest, ReplaceForPersonStoresTheLink) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetTokensStore", [&](Transaction& transaction) {
        PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        int64_t personId = AddTestPerson(transaction, testDb, "store@example.com");
        int64_t expiresAt = NowUs(transaction) + kHourUs;

        int64_t id = tokens.ReplaceForPerson(transaction, personId, "hash-1", expiresAt);
        EXPECT_GT(id, 0);

        KeyValueTable row = tokens.GetForPerson(transaction, personId);
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(Column(row, DbSchema::kPasswordResetTokensId), std::to_string(id));
        EXPECT_EQ(Column(row, DbSchema::kPasswordResetTokensTokenHash), "hash-1");
        EXPECT_EQ(Column(row, DbSchema::kPasswordResetTokensExpiresAt),
                  std::to_string(expiresAt));
        EXPECT_EQ(Column(row, DbSchema::kPasswordResetTokensAttempts), "0");
        EXPECT_FALSE(Column(row, DbSchema::kPasswordResetTokensCreatedAt).empty());
    });
}

TEST(PasswordResetTokensTest, ReplaceForPersonKillsTheEarlierLink) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetTokensReplace", [&](Transaction& transaction) {
        PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        int64_t personId = AddTestPerson(transaction, testDb, "replace@example.com");
        int64_t expiresAt = NowUs(transaction) + kHourUs;

        int64_t first = tokens.ReplaceForPerson(transaction, personId, "old-hash", expiresAt);
        tokens.IncrementAttempts(transaction, personId, 5);
        int64_t second = tokens.ReplaceForPerson(transaction, personId, "new-hash", expiresAt);

        EXPECT_NE(first, second);
        KeyValueTable row = tokens.GetForPerson(transaction, personId);
        EXPECT_EQ(Column(row, DbSchema::kPasswordResetTokensTokenHash), "new-hash");
        // A fresh link starts with a fresh count.
        EXPECT_EQ(Column(row, DbSchema::kPasswordResetTokensAttempts), "0");
        EXPECT_EQ(transaction.RunSqlStatementReturningOneValue(
                      "SELECT count(*) FROM password_reset_tokens WHERE person_id = $1",
                      std::to_string(personId)),
                  "1");
    });
}

TEST(PasswordResetTokensTest, ReplacingOnePersonsLinkLeavesOthersAlone) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetTokensOthers", [&](Transaction& transaction) {
        PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        int64_t alice = AddTestPerson(transaction, testDb, "alice@example.com");
        int64_t bob = AddTestPerson(transaction, testDb, "bob@example.com");
        int64_t expiresAt = NowUs(transaction) + kHourUs;

        tokens.ReplaceForPerson(transaction, alice, "alice-hash", expiresAt);
        tokens.ReplaceForPerson(transaction, bob, "bob-hash", expiresAt);
        tokens.ReplaceForPerson(transaction, alice, "alice-hash-2", expiresAt);

        EXPECT_EQ(Column(tokens.GetForPerson(transaction, bob),
                         DbSchema::kPasswordResetTokensTokenHash),
                  "bob-hash");
    });
}

TEST(PasswordResetTokensTest, GetForPersonIsEmptyWithoutALink) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetTokensNone", [&](Transaction& transaction) {
        PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        int64_t personId = AddTestPerson(transaction, testDb, "none@example.com");
        EXPECT_TRUE(tokens.GetForPerson(transaction, personId).empty());
    });
}

TEST(PasswordResetTokensTest, IncrementAttemptsCountsAndReturnsTheRow) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetTokensIncrement", [&](Transaction& transaction) {
        PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        int64_t personId = AddTestPerson(transaction, testDb, "count@example.com");
        tokens.ReplaceForPerson(
            transaction, personId, "the-hash", NowUs(transaction) + kHourUs);

        KeyValueTable first = tokens.IncrementAttempts(transaction, personId, 5);
        ASSERT_FALSE(first.empty());
        EXPECT_EQ(Column(first, DbSchema::kPasswordResetTokensAttempts), "1");
        EXPECT_EQ(Column(first, DbSchema::kPasswordResetTokensTokenHash), "the-hash");

        KeyValueTable second = tokens.IncrementAttempts(transaction, personId, 5);
        EXPECT_EQ(Column(second, DbSchema::kPasswordResetTokensAttempts), "2");
        EXPECT_EQ(Column(tokens.GetForPerson(transaction, personId),
                         DbSchema::kPasswordResetTokensAttempts),
                  "2");
    });
}

TEST(PasswordResetTokensTest, IncrementAttemptsStopsAtTheLimit) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetTokensLimit", [&](Transaction& transaction) {
        PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        int64_t personId = AddTestPerson(transaction, testDb, "limit@example.com");
        tokens.ReplaceForPerson(
            transaction, personId, "the-hash", NowUs(transaction) + kHourUs);

        EXPECT_FALSE(tokens.IncrementAttempts(transaction, personId, 2).empty());
        EXPECT_FALSE(tokens.IncrementAttempts(transaction, personId, 2).empty());
        EXPECT_TRUE(tokens.IncrementAttempts(transaction, personId, 2).empty());
        // The refused attempt did not count.
        EXPECT_EQ(Column(tokens.GetForPerson(transaction, personId),
                         DbSchema::kPasswordResetTokensAttempts),
                  "2");
    });
}

TEST(PasswordResetTokensTest, IncrementAttemptsIgnoresAnExpiredLink) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetTokensExpired", [&](Transaction& transaction) {
        PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        int64_t personId = AddTestPerson(transaction, testDb, "expired@example.com");
        tokens.ReplaceForPerson(
            transaction, personId, "the-hash", NowUs(transaction) - 1);

        EXPECT_TRUE(tokens.IncrementAttempts(transaction, personId, 5).empty());
        EXPECT_EQ(Column(tokens.GetForPerson(transaction, personId),
                         DbSchema::kPasswordResetTokensAttempts),
                  "0");
    });
}

TEST(PasswordResetTokensTest, IncrementAttemptsIsEmptyWithoutALink) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetTokensIncrementNone", [&](Transaction& transaction) {
        PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        int64_t personId = AddTestPerson(transaction, testDb, "nolink@example.com");
        EXPECT_TRUE(tokens.IncrementAttempts(transaction, personId, 5).empty());
    });
}

TEST(PasswordResetTokensTest, DeleteForPersonRemovesOnlyThatLink) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetTokensDelete", [&](Transaction& transaction) {
        PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        int64_t alice = AddTestPerson(transaction, testDb, "del-alice@example.com");
        int64_t bob = AddTestPerson(transaction, testDb, "del-bob@example.com");
        int64_t expiresAt = NowUs(transaction) + kHourUs;
        tokens.ReplaceForPerson(transaction, alice, "alice-hash", expiresAt);
        tokens.ReplaceForPerson(transaction, bob, "bob-hash", expiresAt);

        tokens.DeleteForPerson(transaction, alice);

        EXPECT_TRUE(tokens.GetForPerson(transaction, alice).empty());
        EXPECT_FALSE(tokens.GetForPerson(transaction, bob).empty());
        // Deleting when there is nothing to delete is not an error.
        EXPECT_NO_THROW(tokens.DeleteForPerson(transaction, alice));
    });
}

TEST(PasswordResetTokensTest, DeletingThePersonDeletesTheLink) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetTokensCascade", [&](Transaction& transaction) {
        PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        int64_t personId = AddTestPerson(transaction, testDb, "gone@example.com");
        tokens.ReplaceForPerson(
            transaction, personId, "the-hash", NowUs(transaction) + kHourUs);

        People people(testDb.GetDatabaseHelper());
        people.DeletePerson(transaction, personId);

        EXPECT_TRUE(tokens.GetForPerson(transaction, personId).empty());
    });
}

}  // namespace
}  // namespace TableHelpers
