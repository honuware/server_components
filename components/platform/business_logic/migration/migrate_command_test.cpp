#include "migrate_command.h"

#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "test/src/util/database_test_helper.h"
#include "test/src/util/test_transaction_provider.h"

namespace Migration {
namespace {

Migration MakeRecordingMigration(
    std::string id, std::vector<std::string>* invocationLog) {
    Migration migration;
    migration.id = std::move(id);
    migration.apply = [migration_id = migration.id, invocationLog](Transaction&) {
        invocationLog->push_back(migration_id);
    };
    return migration;
}

TEST(MigrateCommandTest, EmptyListReturnsZero) {
    // The expected first-deploy / no-changes case: BuildAllMigrations is
    // empty, command runs as a no-op and exits 0 so the operator's
    // install.sh continues.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EmptyListReturnsZero",
        [&](Transaction& transaction) {
        auto tp = MakeTestTransactionProvider(transaction);
        int rc = RunMigrateCommand(*tp, testDb.GetDatabaseHelper(), {});
        EXPECT_EQ(rc, 0);
    });
}

TEST(MigrateCommandTest, AppliesPendingMigrations) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AppliesPendingMigrations",
        [&](Transaction& transaction) {
        auto tp = MakeTestTransactionProvider(transaction);

        std::vector<std::string> log;
        std::vector<Migration> migrations = {
            MakeRecordingMigration("0001_baseline", &log),
            MakeRecordingMigration("0002_subscriptions", &log),
        };
        int rc = RunMigrateCommand(*tp, testDb.GetDatabaseHelper(), migrations);

        EXPECT_EQ(rc, 0);
        ASSERT_EQ(log.size(), 2u);
        EXPECT_EQ(log[0], "0001_baseline");
        EXPECT_EQ(log[1], "0002_subscriptions");
    });
}

TEST(MigrateCommandTest, IsIdempotentAcrossRuns) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsIdempotentAcrossRuns",
        [&](Transaction& transaction) {
        auto tp = MakeTestTransactionProvider(transaction);

        std::vector<std::string> firstLog;
        EXPECT_EQ(RunMigrateCommand(*tp, testDb.GetDatabaseHelper(), {
            MakeRecordingMigration("0001_baseline", &firstLog),
        }), 0);
        ASSERT_EQ(firstLog.size(), 1u);

        // Second run with the same list — the migration is now in
        // schema_migrations, so the apply callback must NOT fire again.
        std::vector<std::string> secondLog;
        EXPECT_EQ(RunMigrateCommand(*tp, testDb.GetDatabaseHelper(), {
            MakeRecordingMigration("0001_baseline", &secondLog),
        }), 0);
        EXPECT_TRUE(secondLog.empty())
            << "second run must not re-invoke the apply callback";
    });
}

TEST(MigrateCommandTest, ReturnsOneOnMigrationFailure) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ReturnsOneOnMigrationFailure",
        [&](Transaction& transaction) {
        auto tp = MakeTestTransactionProvider(transaction);

        Migration failing;
        failing.id = "0001_explodes";
        failing.apply = [](Transaction&) {
            throw std::runtime_error("intentional failure");
        };

        int rc = RunMigrateCommand(
            *tp, testDb.GetDatabaseHelper(), { std::move(failing) });
        EXPECT_EQ(rc, 1);
    });
}

TEST(MigrateCommandTest, StopsAtFailingMigrationAndReturnsOne) {
    // Three migrations: first succeeds, second throws, third must NOT run.
    // Verifies the runner-level "stop on failure" behavior survives the
    // command-level wrapping (no try/catch swallowing inside a loop).
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("StopsAtFailingMigrationAndReturnsOne",
        [&](Transaction& transaction) {
        auto tp = MakeTestTransactionProvider(transaction);

        std::vector<std::string> log;
        std::vector<Migration> migrations;
        migrations.push_back(MakeRecordingMigration("0001_baseline", &log));

        Migration failing;
        failing.id = "0002_explodes";
        failing.apply = [&log](Transaction&) {
            log.push_back("0002_explodes");
            throw std::runtime_error("boom");
        };
        migrations.push_back(std::move(failing));

        migrations.push_back(MakeRecordingMigration("0003_never_runs", &log));

        int rc = RunMigrateCommand(*tp, testDb.GetDatabaseHelper(), migrations);
        EXPECT_EQ(rc, 1);

        // 0001 ran, 0002 ran (then threw), 0003 was never attempted.
        ASSERT_EQ(log.size(), 2u);
        EXPECT_EQ(log[0], "0001_baseline");
        EXPECT_EQ(log[1], "0002_explodes");
    });
}

TEST(MigrateCommandTest, MixedAppliedAndSkippedReturnsZero) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("MixedAppliedAndSkippedReturnsZero",
        [&](Transaction& transaction) {
        auto tp = MakeTestTransactionProvider(transaction);

        // First pass: apply 0001.
        std::vector<std::string> firstLog;
        ASSERT_EQ(RunMigrateCommand(*tp, testDb.GetDatabaseHelper(), {
            MakeRecordingMigration("0001_baseline", &firstLog),
        }), 0);

        // Second pass: 0001 is now applied, 0002 is new. Skipped + applied.
        std::vector<std::string> secondLog;
        int rc = RunMigrateCommand(*tp, testDb.GetDatabaseHelper(), {
            MakeRecordingMigration("0001_baseline", &secondLog),
            MakeRecordingMigration("0002_subscriptions", &secondLog),
        });
        EXPECT_EQ(rc, 0);
        ASSERT_EQ(secondLog.size(), 1u);
        EXPECT_EQ(secondLog[0], "0002_subscriptions");
    });
}

}  // namespace
}  // namespace Migration
