#include "migration_runner.h"

#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "business_logic/migration/migration_namespace.h"
#include "sql_util/table_helpers/schema_migrations.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/test_transaction_provider.h"

namespace Migration {
namespace {

// Build a migration that records its invocation into the caller-supplied
// log. Tests use this to verify both order and presence/absence of calls.
Migration MakeRecordingMigration(
    std::string id, std::vector<std::string>* invocationLog) {
    Migration migration;
    migration.id = std::move(id);
    migration.apply = [migration_id = migration.id, invocationLog](Transaction&) {
        invocationLog->push_back(migration_id);
    };
    return migration;
}

// --- IsApplied / ListApplied ---

TEST(MigrationRunnerTest, IsAppliedFalseForUnknownId) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsAppliedFalseForUnknownId",
        [&](Transaction& transaction) {
        MigrationRunner runner(testDb.GetDatabaseHelper());
        EXPECT_FALSE(runner.IsApplied(transaction, "0001_baseline"));
        EXPECT_FALSE(runner.IsApplied(transaction, ""));
    });
}

TEST(MigrationRunnerTest, IsAppliedTrueAfterRecord) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsAppliedTrueAfterRecord",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        // Use the table helper directly so this test doesn't depend on the
        // ApplyOne code path we test separately.
        TableHelpers::SchemaMigrations schemaMigrations(databaseHelper);
        schemaMigrations.RecordApplied(transaction, "0001_baseline");

        MigrationRunner runner(databaseHelper);
        EXPECT_TRUE(runner.IsApplied(transaction, "0001_baseline"));
        EXPECT_FALSE(runner.IsApplied(transaction, "0002_other"));
    });
}

TEST(MigrationRunnerTest, ListAppliedReturnsEmptyOnFreshDb) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ListAppliedReturnsEmptyOnFreshDb",
        [&](Transaction& transaction) {
        MigrationRunner runner(testDb.GetDatabaseHelper());
        EXPECT_TRUE(runner.ListApplied(transaction).empty());
    });
}

TEST(MigrationRunnerTest, ListAppliedReturnsIdsInOrderApplied) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ListAppliedReturnsIdsInOrderApplied",
        [&](Transaction& transaction) {
        MigrationRunner runner(testDb.GetDatabaseHelper());
        std::vector<std::string> log;
        EXPECT_TRUE(runner.ApplyOne(
            transaction, MakeRecordingMigration("0001_baseline", &log)));
        EXPECT_TRUE(runner.ApplyOne(
            transaction, MakeRecordingMigration("0002_subscriptions", &log)));
        EXPECT_TRUE(runner.ApplyOne(
            transaction, MakeRecordingMigration("0003_vouchers", &log)));

        auto applied = runner.ListApplied(transaction);
        ASSERT_EQ(applied.size(), 3u);
        EXPECT_EQ(applied[0], "0001_baseline");
        EXPECT_EQ(applied[1], "0002_subscriptions");
        EXPECT_EQ(applied[2], "0003_vouchers");
    });
}

// --- ApplyOne ---

TEST(MigrationRunnerTest, ApplyOneCallsApplyAndRecordsId) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ApplyOneCallsApplyAndRecordsId",
        [&](Transaction& transaction) {
        MigrationRunner runner(testDb.GetDatabaseHelper());

        std::vector<std::string> invocationLog;
        Migration m = MakeRecordingMigration("0001_baseline", &invocationLog);
        bool applied = runner.ApplyOne(transaction, m);

        EXPECT_TRUE(applied);
        ASSERT_EQ(invocationLog.size(), 1u);
        EXPECT_EQ(invocationLog[0], "0001_baseline");
        EXPECT_TRUE(runner.IsApplied(transaction, "0001_baseline"));
    });
}

TEST(MigrationRunnerTest, ApplyOneSkipsAlreadyAppliedAndReturnsFalse) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ApplyOneSkipsAlreadyAppliedAndReturnsFalse",
        [&](Transaction& transaction) {
        MigrationRunner runner(testDb.GetDatabaseHelper());

        std::vector<std::string> invocationLog;
        Migration m = MakeRecordingMigration("0001_baseline", &invocationLog);
        ASSERT_TRUE(runner.ApplyOne(transaction, m));

        // Second call: skip, callback NOT invoked again.
        bool applied = runner.ApplyOne(transaction, m);
        EXPECT_FALSE(applied);
        EXPECT_EQ(invocationLog.size(), 1u)
            << "apply callback should not be invoked on the skip path";
    });
}

TEST(MigrationRunnerTest, ApplyOneDoesNotRecordIdWhenApplyThrows) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ApplyOneDoesNotRecordIdWhenApplyThrows",
        [&](Transaction& transaction) {
        MigrationRunner runner(testDb.GetDatabaseHelper());

        Migration m;
        m.id = "0002_will_fail";
        m.apply = [](Transaction&) {
            throw std::runtime_error("boom");
        };

        EXPECT_THROW(runner.ApplyOne(transaction, m), std::runtime_error);

        // The runner did NOT record the row — the exception propagated
        // before the RecordApplied step.
        EXPECT_FALSE(runner.IsApplied(transaction, "0002_will_fail"));
    });
}

TEST(MigrationRunnerTest, ApplyOneApplyCallbackSeesTheSameTransaction) {
    // Migrations are supposed to read and write the same transaction they
    // get passed. This test verifies that contract: the callback's effects
    // are visible to the caller's subsequent reads.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ApplyOneApplyCallbackSeesTheSameTransaction",
        [&](Transaction& transaction) {
        MigrationRunner runner(testDb.GetDatabaseHelper());

        Migration m;
        m.id = "0003_creates_table";
        m.apply = [](Transaction& txn) {
            // Create a temp table — visible only within this transaction.
            txn.RunSqlStatement(
                "CREATE TEMPORARY TABLE migration_smoke (k TEXT)");
            txn.RunSqlStatement(
                "INSERT INTO migration_smoke (k) VALUES ('hello')");
        };

        EXPECT_TRUE(runner.ApplyOne(transaction, m));

        // The callback's writes are visible to the same transaction
        // afterwards.
        std::string value = transaction.RunSqlStatementReturningOneValue(
            "SELECT k FROM migration_smoke LIMIT 1");
        EXPECT_EQ(value, "hello");
    });
}

// --- ApplyPending ---

TEST(MigrationRunnerTest, ApplyPendingEmptyListReturnsEmptyResult) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ApplyPendingEmptyListReturnsEmptyResult",
        [&](Transaction& transaction) {
        MigrationRunner runner(testDb.GetDatabaseHelper());
        auto tp = MakeTestTransactionProvider(transaction);

        auto result = runner.ApplyPending(*tp, {});
        EXPECT_TRUE(result.applied.empty());
        EXPECT_TRUE(result.skipped.empty());
    });
}

TEST(MigrationRunnerTest, ApplyPendingAppliesAllInOrderOnFreshDb) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ApplyPendingAppliesAllInOrderOnFreshDb",
        [&](Transaction& transaction) {
        MigrationRunner runner(testDb.GetDatabaseHelper());
        auto tp = MakeTestTransactionProvider(transaction);

        std::vector<std::string> invocationLog;
        std::vector<Migration> migrations = {
            MakeRecordingMigration("0001_baseline", &invocationLog),
            MakeRecordingMigration("0002_subscriptions", &invocationLog),
            MakeRecordingMigration("0003_vouchers", &invocationLog),
        };

        auto result = runner.ApplyPending(*tp, migrations);

        ASSERT_EQ(result.applied.size(), 3u);
        EXPECT_EQ(result.applied[0], "0001_baseline");
        EXPECT_EQ(result.applied[1], "0002_subscriptions");
        EXPECT_EQ(result.applied[2], "0003_vouchers");
        EXPECT_TRUE(result.skipped.empty());

        ASSERT_EQ(invocationLog.size(), 3u);
        EXPECT_EQ(invocationLog[0], "0001_baseline");
        EXPECT_EQ(invocationLog[1], "0002_subscriptions");
        EXPECT_EQ(invocationLog[2], "0003_vouchers");
    });
}

TEST(MigrationRunnerTest, ApplyPendingSkipsAlreadyAppliedAndAppliesRest) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ApplyPendingSkipsAlreadyAppliedAndAppliesRest",
        [&](Transaction& transaction) {
        MigrationRunner runner(testDb.GetDatabaseHelper());
        auto tp = MakeTestTransactionProvider(transaction);

        // First pass: apply 0001 + 0002.
        std::vector<std::string> firstLog;
        runner.ApplyPending(*tp, {
            MakeRecordingMigration("0001_baseline", &firstLog),
            MakeRecordingMigration("0002_subscriptions", &firstLog),
        });
        ASSERT_EQ(firstLog.size(), 2u);

        // Second pass: same first two plus a new 0003. Skipped ones must NOT
        // re-invoke their apply callbacks.
        std::vector<std::string> secondLog;
        auto result = runner.ApplyPending(*tp, {
            MakeRecordingMigration("0001_baseline", &secondLog),
            MakeRecordingMigration("0002_subscriptions", &secondLog),
            MakeRecordingMigration("0003_vouchers", &secondLog),
        });

        ASSERT_EQ(result.applied.size(), 1u);
        EXPECT_EQ(result.applied[0], "0003_vouchers");
        ASSERT_EQ(result.skipped.size(), 2u);
        EXPECT_EQ(result.skipped[0], "0001_baseline");
        EXPECT_EQ(result.skipped[1], "0002_subscriptions");

        ASSERT_EQ(secondLog.size(), 1u);
        EXPECT_EQ(secondLog[0], "0003_vouchers");
    });
}

TEST(MigrationRunnerTest, ApplyPendingStopsAtFailingMigration) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ApplyPendingStopsAtFailingMigration",
        [&](Transaction& transaction) {
        MigrationRunner runner(testDb.GetDatabaseHelper());
        auto tp = MakeTestTransactionProvider(transaction);

        std::vector<std::string> invocationLog;
        std::vector<Migration> migrations;
        migrations.push_back(
            MakeRecordingMigration("0001_baseline", &invocationLog));

        Migration failing;
        failing.id = "0002_explodes";
        failing.apply = [&invocationLog](Transaction&) {
            invocationLog.push_back("0002_explodes");
            throw std::runtime_error("intentional");
        };
        migrations.push_back(std::move(failing));

        migrations.push_back(
            MakeRecordingMigration("0003_never_runs", &invocationLog));

        try {
            runner.ApplyPending(*tp, migrations);
            FAIL() << "ApplyPending should have re-thrown the failure";
        } catch (const MigrationFailure& e) {
            EXPECT_EQ(e.MigrationId(), "0002_explodes");
            EXPECT_NE(std::string(e.what()).find("intentional"),
                      std::string::npos);
        }

        // 0001 was attempted (and succeeded), 0002 was attempted (and threw),
        // 0003 was NEVER attempted.
        ASSERT_EQ(invocationLog.size(), 2u);
        EXPECT_EQ(invocationLog[0], "0001_baseline");
        EXPECT_EQ(invocationLog[1], "0002_explodes");
    });
}

TEST(MigrationRunnerTest, ApplyPendingPropagatesUnknownExceptionAsFailure) {
    // A non-std::exception thrown from apply must still be wrapped in
    // MigrationFailure rather than escaping the abstraction.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ApplyPendingPropagatesUnknownExceptionAsFailure",
        [&](Transaction& transaction) {
        MigrationRunner runner(testDb.GetDatabaseHelper());
        auto tp = MakeTestTransactionProvider(transaction);

        Migration odd;
        odd.id = "0001_throws_int";
        odd.apply = [](Transaction&) { throw 42; };

        try {
            runner.ApplyPending(*tp, { std::move(odd) });
            FAIL() << "Expected MigrationFailure";
        } catch (const MigrationFailure& e) {
            EXPECT_EQ(e.MigrationId(), "0001_throws_int");
        }
    });
}

TEST(MigrationRunnerTest, NamespacedIdsInDifferentNamespacesBothApply) {
    // Composed-list application + namespace independence (Phase 2.3, `⇦ tenancy`):
    // a framework migration and an app migration that share the same LOCAL id
    // ("0001_baseline") must BOTH apply, because NamespacedMigrationId gives
    // them distinct schema_migrations ids. Without namespacing, the runner's
    // IsApplied gate would skip the second as a duplicate.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("NamespacedIdsInDifferentNamespacesBothApply",
        [&](Transaction& transaction) {
        MigrationRunner runner(testDb.GetDatabaseHelper());
        auto tp = MakeTestTransactionProvider(transaction);

        const std::string frameworkId =
            NamespacedMigrationId(kFrameworkMigrationNamespace, "0001_baseline");
        const std::string appId =
            NamespacedMigrationId(kAppMigrationNamespace, "0001_baseline");
        ASSERT_NE(frameworkId, appId);

        std::vector<std::string> log;
        auto result = runner.ApplyPending(*tp, {
            MakeRecordingMigration(frameworkId, &log),
            MakeRecordingMigration(appId, &log),
        });

        // Both distinct namespaced ids applied — no collision, nothing skipped.
        ASSERT_EQ(result.applied.size(), 2u);
        EXPECT_EQ(result.applied[0], frameworkId);
        EXPECT_EQ(result.applied[1], appId);
        EXPECT_TRUE(result.skipped.empty());
        ASSERT_EQ(log.size(), 2u);

        // Both are recorded independently in schema_migrations.
        EXPECT_TRUE(runner.IsApplied(transaction, frameworkId));
        EXPECT_TRUE(runner.IsApplied(transaction, appId));
    });
}

TEST(MigrationRunnerTest, ApplyPendingReturnsAllSkippedIfNothingNew) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ApplyPendingReturnsAllSkippedIfNothingNew",
        [&](Transaction& transaction) {
        MigrationRunner runner(testDb.GetDatabaseHelper());
        auto tp = MakeTestTransactionProvider(transaction);

        std::vector<std::string> log;
        runner.ApplyPending(*tp, {
            MakeRecordingMigration("0001_baseline", &log),
            MakeRecordingMigration("0002_subscriptions", &log),
        });
        ASSERT_EQ(log.size(), 2u);

        // Apply the same list again — everything is skipped.
        std::vector<std::string> secondLog;
        auto result = runner.ApplyPending(*tp, {
            MakeRecordingMigration("0001_baseline", &secondLog),
            MakeRecordingMigration("0002_subscriptions", &secondLog),
        });
        EXPECT_TRUE(result.applied.empty());
        ASSERT_EQ(result.skipped.size(), 2u);
        EXPECT_EQ(result.skipped[0], "0001_baseline");
        EXPECT_EQ(result.skipped[1], "0002_subscriptions");
        EXPECT_TRUE(secondLog.empty()) << "skipped migrations must not run";
    });
}

}  // namespace
}  // namespace Migration
