#include "framework_migrations.h"

#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "business_logic/create_framework_tables.h"
#include "db_schema/admin_column_data_info.h"
#include "db_schema/password_reset_tokens.h"
#include "db_schema/people.h"
#include "db_schema/role_assignments.h"
#include "db_schema/role_permissions.h"
#include "db_schema/site_assets.h"
#include "migration_namespace.h"
#include "migration_runner.h"
#include "sql_util/database_access/db_and_table_operations.h"
#include "sql_util/schema/database_info.h"
#include "sql_util/table_helpers/admin_column_data_info.h"
#include "sql_util/table_helpers/password_reset_tokens.h"
#include "sql_util/table_helpers/people.h"
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

TEST(FrameworkMigrationsTest, PasswordResetTokensMigrationCreatesTheTable) {
    // Finishing website Phase 11.1: a database installed before the
    // forgot-password table existed gets it from `--migrate`.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("PasswordResetTokensMigration", [&](Transaction& transaction) {
        DbOps::DropTable(transaction, DbSchema::kPasswordResetTokensTable);

        const std::vector<Migration> migrations = BuildFrameworkMigrations();
        const Migration* passwordReset = nullptr;
        for (const Migration& migration : migrations) {
            if (migration.id == NamespacedMigrationId(
                    kFrameworkMigrationNamespace, "0002_password_reset_tokens")) {
                passwordReset = &migration;
            }
        }
        ASSERT_NE(passwordReset, nullptr)
            << "the password_reset_tokens migration is missing from the chain";

        passwordReset->apply(transaction);

        // Usable through the production helper, not merely present.
        TableHelpers::People people(testDb.GetDatabaseHelper());
        int64_t personId = people.AddPerson(
            transaction, "migrated@example.com", "F", "L", "hash");
        TableHelpers::PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        tokens.ReplaceForPerson(transaction, personId, "the-hash", 1);
        EXPECT_EQ(tokens.GetForPerson(transaction, personId)
                      .at(std::string(DbSchema::kPasswordResetTokensTokenHash)),
                  "the-hash");
        // The foreign key came with it: deleting the person deletes the link.
        people.DeletePerson(transaction, personId);
        EXPECT_TRUE(tokens.GetForPerson(transaction, personId).empty());

        // And a second run (the next deploy) leaves it alone.
        EXPECT_NO_THROW(passwordReset->apply(transaction));
    });
}

// Finishing website Phase 20.5: a database seeded before the fix still has the
// three role foreign keys marked read-only, which hides them on the CRUD
// editor's New form. Read through the helper /api/get_db_schema reads.
TEST(FrameworkMigrationsTest, RolePickersMigrationClearsTheReadOnlyFlags) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RolePickersMigration", [&](Transaction& transaction) {
        DbSchema::DatabaseInfo info("framework_test");
        PopulateFrameworkTables(transaction, testDb.GetDatabaseHelper(), info);

        const std::pair<std::string_view, std::string_view> roleColumns[] = {
            { DbSchema::kRoleAssignmentsTable, DbSchema::kRoleAssignmentsPersonId },
            { DbSchema::kRoleAssignmentsTable, DbSchema::kRoleAssignmentsRoleId },
            { DbSchema::kRolePermissionsTable, DbSchema::kRolePermissionsRoleId },
        };
        // Put the old seed's flags back, as on a database installed before it.
        for (const auto& [table, column] : roleColumns) {
            transaction.RunSqlStatement(
                "UPDATE admin_column_data_info SET readonly = true "
                "WHERE table_name = $1 AND column_name = $2",
                std::string(table), std::string(column));
        }

        TableHelpers::AdminColumnDataInfo dataInfo(testDb.GetDatabaseHelper());
        auto readonlyOf = [&](std::string_view table, std::string_view column) {
            KeyValueTable row;
            EXPECT_TRUE(dataInfo.GetAdminColumnDataInfo(transaction, table, column, row))
                << table << "." << column;
            return row[std::string(DbSchema::kAdminColumnDataInfoReadonly)];
        };
        for (const auto& [table, column] : roleColumns) {
            ASSERT_EQ(readonlyOf(table, column), "t") << table << "." << column;
        }

        const std::vector<Migration> migrations = BuildFrameworkMigrations();
        const Migration* rolePickers = nullptr;
        for (const Migration& migration : migrations) {
            if (migration.id == NamespacedMigrationId(
                    kFrameworkMigrationNamespace, "0003_role_pickers")) {
                rolePickers = &migration;
            }
        }
        ASSERT_NE(rolePickers, nullptr)
            << "the role_pickers migration is missing from the chain";

        rolePickers->apply(transaction);

        for (const auto& [table, column] : roleColumns) {
            const std::string readonly = readonlyOf(table, column);
            EXPECT_TRUE(readonly.empty() || readonly == "f")
                << table << "." << column << " is still read-only";
        }
        // A column that really is read-only keeps its flag.
        EXPECT_EQ(readonlyOf(DbSchema::kPeopleTable, DbSchema::kPeopleCreatedAt), "t");

        // And a second run (the next deploy) changes nothing.
        EXPECT_NO_THROW(rolePickers->apply(transaction));
        EXPECT_EQ(readonlyOf(DbSchema::kPeopleTable, DbSchema::kPeopleCreatedAt), "t");
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
        DbOps::DropTable(transaction, DbSchema::kPasswordResetTokensTable);
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
