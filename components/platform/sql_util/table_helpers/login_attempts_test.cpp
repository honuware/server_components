#include "sql_util/table_helpers/login_attempts.h"

#include <thread>
#include <chrono>

#include <gtest/gtest.h>

#include "db_schema/login_attempts.h"
#include "test/src/util/database_test_helper.h"

// Phase 5.1 of the security review.
//
// Cover the full surface of the helper:
//   - RecordAttempt inserts a row with the right shape
//   - The success/failure flag is honoured (only failures are counted)
//   - Per-email and per-IP counts respect the (kind, window) bounds
//   - Old rows fall outside the window via attempted_at < cutoff
//   - PurgeOlderThan deletes only rows older than the threshold
//
// The window-based assertions use SECONDS-scale windows; tests that
// assert "outside the window" insert a row, sleep briefly, then
// query with a window narrower than the elapsed time. now_us() ticks
// at microsecond resolution so the deltas are reliable.

namespace TableHelpers {
namespace {

TEST(LoginAttemptsTest, RecordAttemptInsertsRow) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RecordAttemptInsertsRow", [&](Transaction& tx) {
        LoginAttempts helper(testDb.GetDatabaseHelper());
        helper.RecordAttempt(
            tx, "user@example.com", "203.0.113.7",
            DbSchema::kLoginAttemptsKindLogin, /*success=*/false);

        std::string count = tx.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM login_attempts");
        EXPECT_EQ(count, "1");

        KeyValueTable row = tx.RunSqlStatementReturningOneRow(
            "SELECT email_lower, ip, success, kind "
            "FROM login_attempts ORDER BY id DESC LIMIT 1");
        EXPECT_EQ(row.at("email_lower"), "user@example.com");
        EXPECT_EQ(row.at("ip"), "203.0.113.7");
        // Postgres BOOL renders as "f" / "t" by default in the
        // helper layer.
        EXPECT_EQ(row.at("success"), "f");
        EXPECT_EQ(row.at("kind"),
                  std::string(DbSchema::kLoginAttemptsKindLogin));
    });
}

TEST(LoginAttemptsTest, EmailLowerIsCaseInsensitive) {
    // Inserted with mixed case; counted with lower case. CITEXT
    // collation guarantees the count picks it up.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EmailLowerIsCaseInsensitive", [&](Transaction& tx) {
        LoginAttempts helper(testDb.GetDatabaseHelper());
        helper.RecordAttempt(
            tx, "Mixed@Example.COM", "203.0.113.7",
            DbSchema::kLoginAttemptsKindLogin, /*success=*/false);

        int64_t cnt = helper.RecentFailureCountForEmail(
            tx, "mixed@example.com",
            DbSchema::kLoginAttemptsKindLogin,
            /*windowMicros=*/60LL * 1000000LL);
        EXPECT_EQ(cnt, 1);
    });
}

TEST(LoginAttemptsTest, RecentFailureCountForEmailExcludesSuccesses) {
    // Successful attempts are stored (for forensics) but MUST NOT
    // count toward the rate-limit threshold. Otherwise a legitimate
    // user who logs in many times in a session could be locked out.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "RecentFailureCountForEmailExcludesSuccesses", [&](Transaction& tx) {
        LoginAttempts helper(testDb.GetDatabaseHelper());
        helper.RecordAttempt(
            tx, "u@example.com", "1.1.1.1",
            DbSchema::kLoginAttemptsKindLogin, /*success=*/false);
        helper.RecordAttempt(
            tx, "u@example.com", "1.1.1.1",
            DbSchema::kLoginAttemptsKindLogin, /*success=*/true);
        helper.RecordAttempt(
            tx, "u@example.com", "1.1.1.1",
            DbSchema::kLoginAttemptsKindLogin, /*success=*/false);

        int64_t cnt = helper.RecentFailureCountForEmail(
            tx, "u@example.com",
            DbSchema::kLoginAttemptsKindLogin,
            /*windowMicros=*/60LL * 1000000LL);
        EXPECT_EQ(cnt, 2)
            << "successful attempt must not count toward failures";
    });
}

TEST(LoginAttemptsTest, RecentFailureCountForEmailIsScopedByKind) {
    // A `verify` failure is counted under the verify bucket only.
    // Login uses its own bucket. Otherwise an aggressive verify
    // attacker could lock out a user's login path.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "RecentFailureCountForEmailIsScopedByKind", [&](Transaction& tx) {
        LoginAttempts helper(testDb.GetDatabaseHelper());
        helper.RecordAttempt(
            tx, "u@example.com", "1.1.1.1",
            DbSchema::kLoginAttemptsKindVerify, /*success=*/false);

        int64_t loginCnt = helper.RecentFailureCountForEmail(
            tx, "u@example.com",
            DbSchema::kLoginAttemptsKindLogin,
            /*windowMicros=*/60LL * 1000000LL);
        int64_t verifyCnt = helper.RecentFailureCountForEmail(
            tx, "u@example.com",
            DbSchema::kLoginAttemptsKindVerify,
            /*windowMicros=*/60LL * 1000000LL);
        EXPECT_EQ(loginCnt, 0);
        EXPECT_EQ(verifyCnt, 1);
    });
}

TEST(LoginAttemptsTest, RecentFailureCountForEmailRespectsWindow) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "RecentFailureCountForEmailRespectsWindow", [&](Transaction& tx) {
        LoginAttempts helper(testDb.GetDatabaseHelper());

        // Insert with attempted_at way in the past — a sentinel
        // "old" row.
        tx.RunSqlStatement(
            "INSERT INTO login_attempts "
            "(email_lower, ip, attempted_at, success, kind) "
            "VALUES ($1, $2, $3, $4, $5)",
            std::string("u@example.com"),
            std::string("1.1.1.1"),
            std::string("1"),  // very old
            std::string("f"),
            std::string(DbSchema::kLoginAttemptsKindLogin));

        // And one fresh row.
        helper.RecordAttempt(
            tx, "u@example.com", "1.1.1.1",
            DbSchema::kLoginAttemptsKindLogin, /*success=*/false);

        // Wide window: both visible.
        int64_t wide = helper.RecentFailureCountForEmail(
            tx, "u@example.com",
            DbSchema::kLoginAttemptsKindLogin,
            /*windowMicros=*/100LL * 365LL * 24LL * 60LL * 60LL * 1000000LL);
        EXPECT_EQ(wide, 2);

        // Narrow window (a few seconds): only the fresh row.
        int64_t narrow = helper.RecentFailureCountForEmail(
            tx, "u@example.com",
            DbSchema::kLoginAttemptsKindLogin,
            /*windowMicros=*/5LL * 1000000LL);
        EXPECT_EQ(narrow, 1);
    });
}

TEST(LoginAttemptsTest, RecentFailureCountForIpScopedByIp) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "RecentFailureCountForIpScopedByIp", [&](Transaction& tx) {
        LoginAttempts helper(testDb.GetDatabaseHelper());
        helper.RecordAttempt(
            tx, "a@example.com", "10.0.0.1",
            DbSchema::kLoginAttemptsKindLogin, /*success=*/false);
        helper.RecordAttempt(
            tx, "b@example.com", "10.0.0.1",
            DbSchema::kLoginAttemptsKindLogin, /*success=*/false);
        helper.RecordAttempt(
            tx, "c@example.com", "10.0.0.2",
            DbSchema::kLoginAttemptsKindLogin, /*success=*/false);

        int64_t ip1 = helper.RecentFailureCountForIp(
            tx, "10.0.0.1",
            DbSchema::kLoginAttemptsKindLogin,
            /*windowMicros=*/60LL * 1000000LL);
        int64_t ip2 = helper.RecentFailureCountForIp(
            tx, "10.0.0.2",
            DbSchema::kLoginAttemptsKindLogin,
            /*windowMicros=*/60LL * 1000000LL);
        EXPECT_EQ(ip1, 2);
        EXPECT_EQ(ip2, 1);
    });
}

TEST(LoginAttemptsTest, RecentFailureCountForIpExcludesSuccesses) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "RecentFailureCountForIpExcludesSuccesses", [&](Transaction& tx) {
        LoginAttempts helper(testDb.GetDatabaseHelper());
        helper.RecordAttempt(
            tx, "u@example.com", "10.0.0.1",
            DbSchema::kLoginAttemptsKindLogin, /*success=*/true);
        helper.RecordAttempt(
            tx, "u@example.com", "10.0.0.1",
            DbSchema::kLoginAttemptsKindLogin, /*success=*/false);

        int64_t cnt = helper.RecentFailureCountForIp(
            tx, "10.0.0.1",
            DbSchema::kLoginAttemptsKindLogin,
            /*windowMicros=*/60LL * 1000000LL);
        EXPECT_EQ(cnt, 1);
    });
}

TEST(LoginAttemptsTest, ZeroOrNegativeWindowReturnsZero) {
    // Edge guard: a misconfigured secret of "0" or "-1" must not
    // cause the gate to short-circuit to "everything is recent" or
    // throw — it returns 0 so the gate makes the safe call (allow).
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ZeroOrNegativeWindowReturnsZero", [&](Transaction& tx) {
        LoginAttempts helper(testDb.GetDatabaseHelper());
        helper.RecordAttempt(
            tx, "u@example.com", "1.1.1.1",
            DbSchema::kLoginAttemptsKindLogin, /*success=*/false);

        EXPECT_EQ(helper.RecentFailureCountForEmail(
            tx, "u@example.com",
            DbSchema::kLoginAttemptsKindLogin, /*windowMicros=*/0), 0);
        EXPECT_EQ(helper.RecentFailureCountForIp(
            tx, "1.1.1.1",
            DbSchema::kLoginAttemptsKindLogin, /*windowMicros=*/-1), 0);
    });
}

TEST(LoginAttemptsTest, PurgeOlderThanBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("PurgeOlderThanBasic", [&](Transaction& tx) {
        LoginAttempts helper(testDb.GetDatabaseHelper());

        // Old row: attempted_at = 1 (effectively 1970).
        tx.RunSqlStatement(
            "INSERT INTO login_attempts "
            "(email_lower, ip, attempted_at, success, kind) "
            "VALUES ($1, $2, $3, $4, $5)",
            std::string("old@example.com"),
            std::string("1.1.1.1"),
            std::string("1"),
            std::string("f"),
            std::string(DbSchema::kLoginAttemptsKindLogin));

        // Fresh row.
        helper.RecordAttempt(
            tx, "fresh@example.com", "1.1.1.1",
            DbSchema::kLoginAttemptsKindLogin, /*success=*/false);

        // Purge anything older than 5 seconds. The fresh row stays;
        // the 1970 row is gone.
        int64_t deleted = helper.PurgeOlderThan(tx, 5LL * 1000000LL);
        EXPECT_EQ(deleted, 1);

        std::string remaining = tx.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM login_attempts");
        EXPECT_EQ(remaining, "1");
        std::string survivor = tx.RunSqlStatementReturningOneValue(
            "SELECT email_lower FROM login_attempts");
        EXPECT_EQ(survivor, "fresh@example.com");
    });
}

TEST(LoginAttemptsTest, PurgeOlderThanZeroIsNoop) {
    // Defensive: a misconfigured retention of "0" must not delete
    // every row (which would lock the gate's view to empty).
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("PurgeOlderThanZeroIsNoop", [&](Transaction& tx) {
        LoginAttempts helper(testDb.GetDatabaseHelper());
        helper.RecordAttempt(
            tx, "u@example.com", "1.1.1.1",
            DbSchema::kLoginAttemptsKindLogin, /*success=*/false);

        EXPECT_EQ(helper.PurgeOlderThan(tx, 0), 0);
        EXPECT_EQ(helper.PurgeOlderThan(tx, -1), 0);

        std::string count = tx.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM login_attempts");
        EXPECT_EQ(count, "1");
    });
}

}  // namespace
}  // namespace TableHelpers
