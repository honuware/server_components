#include "framework_migrations.h"

#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "db_schema/site_assets.h"
#include "migration_namespace.h"
#include "migration_runner.h"
#include "sql_util/database_access/db_and_table_operations.h"
#include "sql_util/table_helpers/site_assets.h"
#include "test/src/util/database_test_helper.h"

namespace Migration {
namespace {

TEST(FrameworkMigrationsTest, EveryIdIsNamespacedAndUnique) {
    // Framework and app migrations share one schema_migrations table, so a bare
    // id like "0001_x" from each side would collide and the second would be
    // silently skipped as already-applied.
    std::set<std::string> ids;
    const std::vector<Migration> migrations = BuildFrameworkMigrations();
    for (const Migration& migration : migrations) {
        EXPECT_EQ(migration.id.rfind(kFrameworkMigrationNamespace, 0), 0u)
            << migration.id << " is not in the framework namespace";
        EXPECT_TRUE(ids.insert(migration.id).second)
            << migration.id << " appears twice";
        EXPECT_TRUE(static_cast<bool>(migration.apply)) << migration.id;
    }
}

TEST(FrameworkMigrationsTest, SiteAssetsMigrationCreatesTheTable) {
    // The bug this exists for: registering site_assets in MakeFrameworkTables
    // covers a database built from scratch and does nothing for one that
    // already exists, so importing a theme into a live site failed with
    // `relation "site_assets" does not exist`.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SiteAssetsMigration", [&](Transaction& transaction) {
        // The harness pre-creates every table, so start from the state a real
        // pre-Phase-9 database is in.
        DbOps::DropTable(transaction, DbSchema::kSiteAssets);

        // Held in a NAMED local. BuildFrameworkMigrations() returns by value, so
        // a range-for over the call site lifetime-extends the vector only for
        // the loop — a pointer into it dangles the moment the loop ends. That
        // read survived on Linux and was an access violation on MSVC, which
        // then corrupted the heap and took ~20 unrelated tests down with it.
        const std::vector<Migration> migrations = BuildFrameworkMigrations();
        const Migration* siteAssets = nullptr;
        for (const Migration& migration : migrations) {
            if (migration.id.find("site_assets") != std::string::npos) {
                siteAssets = &migration;
            }
        }
        ASSERT_NE(siteAssets, nullptr)
            << "the site_assets migration is missing from the chain";

        siteAssets->apply(transaction);

        // Usable, not merely present: a CREATE TABLE that produced the wrong
        // columns would still satisfy a bare existence check.
        TableHelpers::SiteAssets assets(testDb.GetDatabaseHelper());
        const std::string bytes = std::string("\x89PNG\r\n\x1a\n", 8) + "image";
        assets.PutAsset(transaction, "logo.png", "png", bytes);
        EXPECT_EQ(assets.GetAssetBytes(transaction, "logo.png"), bytes);
    });
}

TEST(FrameworkMigrationsTest, TheChainIsSafeOnAFreshDatabaseThatAlreadyHasEverything) {
    // The case a migration chain is easiest to get wrong: `--recreate_database`
    // builds every table and records NOTHING in schema_migrations, so the next
    // `--migrate` sees the whole chain as pending and runs it against a
    // database that already has all of it. A plain CREATE TABLE there fails.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("MigrationsFreshDb", [&](Transaction& transaction) {
        // The harness pre-creates every table, which IS the fresh-database
        // state — so just run the chain against it.
        const std::vector<Migration> migrations = BuildFrameworkMigrations();
        for (const Migration& migration : migrations) {
            EXPECT_NO_THROW(migration.apply(transaction)) << migration.id;
        }
    });
}

TEST(FrameworkMigrationsTest, RunningTheChainTwiceIsSafe) {
    // Deploys re-run migrate; the second pass must skip rather than fail on a
    // table that is already there.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("MigrationsTwice", [&](Transaction& transaction) {
        DbOps::DropTable(transaction, DbSchema::kSiteAssets);
        MigrationRunner runner(testDb.GetDatabaseHelper());
        const std::vector<Migration> migrations = BuildFrameworkMigrations();
        for (const Migration& migration : migrations) {
            EXPECT_TRUE(runner.ApplyOne(transaction, migration)) << migration.id;
        }
        for (const Migration& migration : migrations) {
            EXPECT_FALSE(runner.ApplyOne(transaction, migration))
                << migration.id << " re-applied instead of being skipped";
        }
    });
}

}  // namespace
}  // namespace Migration
