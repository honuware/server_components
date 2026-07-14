#include "sql_util/table_helpers/auth_events.h"

#include <gtest/gtest.h>

#include "db_schema/auth_events.h"
#include "test/src/util/database_test_helper.h"

// Phase 9.1 of the security review.

namespace TableHelpers {
namespace {

TEST(AuthEventsTest, RecordEventInsertsRow) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RecordEventInsertsRow", [&](Transaction& tx) {
        AuthEvents helper(testDb.GetDatabaseHelper());

        helper.RecordEvent(tx,
            DbSchema::kAuthEventsKindLoginSuccess,
            /*personId=*/42,
            "203.0.113.7",
            "TestUserAgent/1.0",
            R"({"path":"/api/login"})");

        std::string count = tx.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM auth_events");
        EXPECT_EQ(count, "1");

        KeyValueTable row = tx.RunSqlStatementReturningOneRow(
            "SELECT kind, person_id, ip, user_agent, detail_json "
            "FROM auth_events ORDER BY id DESC LIMIT 1");
        EXPECT_EQ(row.at("kind"),
                  std::string(DbSchema::kAuthEventsKindLoginSuccess));
        EXPECT_EQ(row.at("person_id"), "42");
        EXPECT_EQ(row.at("ip"), "203.0.113.7");
        EXPECT_EQ(row.at("user_agent"), "TestUserAgent/1.0");
        EXPECT_EQ(row.at("detail_json"), R"({"path":"/api/login"})");
    });
}

TEST(AuthEventsTest, ZeroPersonIdIsStoredAsNull) {
    // login_failure on unknown email: the row records the event
    // but person_id is NULL since we couldn't identify a user.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ZeroPersonIdIsStoredAsNull", [&](Transaction& tx) {
        AuthEvents helper(testDb.GetDatabaseHelper());

        helper.RecordEvent(tx,
            DbSchema::kAuthEventsKindLoginFailure,
            /*personId=*/0,
            "1.2.3.4",
            "UA",
            "");

        std::string nullCount = tx.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM auth_events WHERE person_id IS NULL");
        EXPECT_EQ(nullCount, "1");
    });
}

TEST(AuthEventsTest, NegativePersonIdIsStoredAsNull) {
    // Defensive: a caller passing -1 or any other negative sentinel
    // should still produce a NULL row, not a literal -1 person_id.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("NegativePersonIdIsStoredAsNull",
        [&](Transaction& tx) {
        AuthEvents helper(testDb.GetDatabaseHelper());
        helper.RecordEvent(tx,
            DbSchema::kAuthEventsKindLoginFailure,
            /*personId=*/-1,
            "1.2.3.4", "UA", "");

        std::string nullCount = tx.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM auth_events WHERE person_id IS NULL");
        EXPECT_EQ(nullCount, "1");
    });
}

TEST(AuthEventsTest, RecordEventTimestampsWithNowUs) {
    // when_us is filled by the SQL `now_us()` function at insert
    // time, not by anything the caller controls — so a write-
    // behind / batched recorder can't backdate rows.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RecordEventTimestampsWithNowUs",
        [&](Transaction& tx) {
        AuthEvents helper(testDb.GetDatabaseHelper());

        std::string beforeStr = tx.RunSqlStatementReturningOneValue(
            "SELECT now_us()");
        int64_t before = std::stoll(beforeStr);

        helper.RecordEvent(tx, DbSchema::kAuthEventsKindLoginSuccess,
            1, "ip", "ua", "");

        std::string whenStr = tx.RunSqlStatementReturningOneValue(
            "SELECT when_us FROM auth_events ORDER BY id DESC LIMIT 1");
        int64_t when = std::stoll(whenStr);

        std::string afterStr = tx.RunSqlStatementReturningOneValue(
            "SELECT now_us()");
        int64_t after = std::stoll(afterStr);

        EXPECT_GE(when, before);
        EXPECT_LE(when, after);
    });
}

TEST(AuthEventsTest, CountRecentByKindScopesToKind) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("CountRecentByKindScopesToKind",
        [&](Transaction& tx) {
        AuthEvents helper(testDb.GetDatabaseHelper());
        helper.RecordEvent(tx, DbSchema::kAuthEventsKindLoginSuccess, 1, "", "", "");
        helper.RecordEvent(tx, DbSchema::kAuthEventsKindLoginSuccess, 2, "", "", "");
        helper.RecordEvent(tx, DbSchema::kAuthEventsKindLoginFailure, 0, "", "", "");

        EXPECT_EQ(helper.CountRecentByKind(
            tx, DbSchema::kAuthEventsKindLoginSuccess,
            /*windowMicros=*/60LL * 1000000LL), 2);
        EXPECT_EQ(helper.CountRecentByKind(
            tx, DbSchema::kAuthEventsKindLoginFailure,
            60LL * 1000000LL), 1);
        EXPECT_EQ(helper.CountRecentByKind(
            tx, DbSchema::kAuthEventsKindPasswordChanged,
            60LL * 1000000LL), 0);
    });
}

TEST(AuthEventsTest, CountRecentByKindRespectsWindow) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("CountRecentByKindRespectsWindow",
        [&](Transaction& tx) {
        AuthEvents helper(testDb.GetDatabaseHelper());

        // Old row (attempted_at=1 ≈ 1970) — outside any reasonable
        // window. Inserted directly so we don't depend on the
        // helper to backdate.
        tx.RunSqlStatement(
            "INSERT INTO auth_events "
            "(kind, person_id, ip, user_agent, when_us, detail_json) "
            "VALUES ($1, $2, $3, $4, $5, $6)",
            std::string(DbSchema::kAuthEventsKindLoginSuccess),
            std::string("1"),
            std::string("1.1.1.1"),
            std::string("UA"),
            std::string("1"),  // very old
            std::string(""));

        // Fresh row.
        helper.RecordEvent(tx, DbSchema::kAuthEventsKindLoginSuccess,
            1, "1.1.1.1", "UA", "");

        // Wide window: both visible.
        EXPECT_EQ(helper.CountRecentByKind(
            tx, DbSchema::kAuthEventsKindLoginSuccess,
            100LL * 365LL * 24LL * 60LL * 60LL * 1000000LL), 2);
        // Narrow window (5 s): only the fresh row.
        EXPECT_EQ(helper.CountRecentByKind(
            tx, DbSchema::kAuthEventsKindLoginSuccess,
            5LL * 1000000LL), 1);
    });
}

TEST(AuthEventsTest, RecentByPersonAndKindFiltersAndOrders) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RecentByPersonAndKindFiltersAndOrders",
        [&](Transaction& tx) {
        AuthEvents helper(testDb.GetDatabaseHelper());

        helper.RecordEvent(tx, DbSchema::kAuthEventsKindLoginSuccess,
            1, "ip1", "ua1", "first");
        helper.RecordEvent(tx, DbSchema::kAuthEventsKindLoginFailure,
            1, "ip1", "ua1", "ignored-other-kind");
        helper.RecordEvent(tx, DbSchema::kAuthEventsKindLoginSuccess,
            2, "ip2", "ua2", "ignored-other-person");
        helper.RecordEvent(tx, DbSchema::kAuthEventsKindLoginSuccess,
            1, "ip1", "ua1", "second");

        auto rows = helper.RecentByPersonAndKind(
            tx, /*personId=*/1,
            DbSchema::kAuthEventsKindLoginSuccess,
            60LL * 1000000LL);
        ASSERT_EQ(rows.size(), 2u);
        // Most-recent first.
        EXPECT_EQ(rows[0].at("detail_json"), "second");
        EXPECT_EQ(rows[1].at("detail_json"), "first");
    });
}

TEST(AuthEventsTest, ZeroOrNegativeWindowReturnsZero) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ZeroOrNegativeWindowReturnsZero",
        [&](Transaction& tx) {
        AuthEvents helper(testDb.GetDatabaseHelper());
        helper.RecordEvent(tx, DbSchema::kAuthEventsKindLoginSuccess,
            1, "", "", "");

        EXPECT_EQ(helper.CountRecentByKind(
            tx, DbSchema::kAuthEventsKindLoginSuccess, 0), 0);
        EXPECT_EQ(helper.CountRecentByKind(
            tx, DbSchema::kAuthEventsKindLoginSuccess, -1), 0);
        EXPECT_TRUE(helper.RecentByPersonAndKind(
            tx, 1, DbSchema::kAuthEventsKindLoginSuccess, 0).empty());
    });
}

}  // namespace
}  // namespace TableHelpers
