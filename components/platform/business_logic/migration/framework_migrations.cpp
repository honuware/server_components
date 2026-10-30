#include "framework_migrations.h"

#include <string>
#include <string_view>
#include <utility>

#include "db_schema/admin_column_data_info.h"
#include "db_schema/make_framework_tables.h"
#include "db_schema/password_reset_tokens.h"
#include "db_schema/role_assignments.h"
#include "db_schema/role_permissions.h"
#include "db_schema/site_assets.h"
#include "migration_namespace.h"
#include "sql_util/database_access/database_metadata.h"
#include "sql_util/database_access/db_and_table_operations.h"
#include "sql_util/schema/database_info.h"

namespace Migration {

std::vector<Migration> BuildFrameworkMigrations() {
    // Framework (honuware) schema changes that must run against an
    // already-provisioned database. Each id is built with
    // NamespacedMigrationId(kFrameworkMigrationNamespace, "NNNN_name") so it
    // stays in the honuware id namespace.
    return {
        // Tenant Theming Phase 9 — site_assets holds the images a theme bundle
        // carries (a logo, a favicon, a hero).
        //
        // ⚠️ THIS COMMENT USED TO BE WRONG, and the wrongness hid a real bug
        // for the table's whole life. It read: "Registering the table in
        // MakeFrameworkTables covers a database built from scratch; it does
        // NOTHING for one that already exists."
        //
        // The first half was false. Registering a table in MakeFrameworkTables
        // gives it DDL; it does not create it. `CreateFrameworkTables` has the
        // list of tables actually built, and site_assets was never on it — so
        // NO database, however freshly created, had this table, and this
        // migration was the only thing that ever produced one. A site that had
        // never run --migrate could not store a theme's images, and the error
        // arrived at APPLY time after the dry run had reported success.
        //
        // CreateFrameworkTables now builds it, guarded by
        // CreateFrameworkTablesMatchesMakeFrameworkTables. This migration stays
        // for databases created before that fix — which is every database that
        // predates it.
        Migration{
            NamespacedMigrationId(kFrameworkMigrationNamespace,
                                  "0001_site_assets"),
            [](Transaction& transaction) {
                // Skip when the table is already there, because a FRESH
                // database has every table (MakeFrameworkTables built them) and
                // an EMPTY schema_migrations — so the first `--migrate` after a
                // `--recreate_database` would otherwise try to create a table
                // that exists and fail. A migration has to be safe against a
                // database in any state, not just the one it was written for.
                for (const std::string& table : DbMeta::ListTables(transaction)) {
                    if (table == DbSchema::kSiteAssets) {
                        return;
                    }
                }
                // A DatabaseInfo carrying only this table: the DDL generator
                // needs the column metadata, and the framework cannot reach the
                // app's full MakeDatabaseInfo. The name is unused by CREATE
                // TABLE — the connection already decides which database this
                // lands in.
                DbSchema::DatabaseInfo databaseInfo("");
                DbSchema::MakeSiteAssetsTable(databaseInfo);
                DbOps::CreateTable(
                    transaction, databaseInfo, DbSchema::kSiteAssets);
            },
        },
        // Finishing website Phase 11.1 — the forgot-password links. A new
        // database gets the table from CreateFrameworkTables; this brings it
        // to one installed before the table existed.
        Migration{
            NamespacedMigrationId(kFrameworkMigrationNamespace,
                                  "0002_password_reset_tokens"),
            [](Transaction& transaction) {
                // Skipped when present, for the same reason as 0001: a fresh
                // database already has it and an empty schema_migrations.
                for (const std::string& table : DbMeta::ListTables(transaction)) {
                    if (table == DbSchema::kPasswordResetTokensTable) {
                        return;
                    }
                }
                // Unlike site_assets, this table has a foreign key (onto
                // people), and the DDL generator types that column by looking
                // up the PARENT's metadata. A DatabaseInfo holding only this
                // table cannot describe it, so describe every framework table
                // and create just this one.
                DbSchema::DatabaseInfo databaseInfo("");
                DbSchema::MakeFrameworkTables(databaseInfo);
                DbOps::CreateTable(
                    transaction, databaseInfo,
                    DbSchema::kPasswordResetTokensTable);
            },
        },
        // Finishing website Phase 20.5 — the CRUD editor's New Role Assignments
        // form had no fields. The seed marked these three foreign keys
        // read-only, and the editor hides a read-only column on New and never
        // sends it. The seed no longer sets the flag; this clears it on a
        // database seeded before that. Only an UPDATE, so a fresh database (no
        // flag to clear) and a second run both change nothing.
        Migration{
            NamespacedMigrationId(kFrameworkMigrationNamespace,
                                  "0003_role_pickers"),
            [](Transaction& transaction) {
                const std::string sql =
                    "UPDATE " + std::string(DbSchema::kAdminColumnDataInfoTable)
                    + " SET " + std::string(DbSchema::kAdminColumnDataInfoReadonly)
                    + " = NULL WHERE "
                    + std::string(DbSchema::kAdminColumnDataInfoTableName) + " = $1 AND "
                    + std::string(DbSchema::kAdminColumnDataInfoColumnName) + " = $2";
                const std::pair<std::string_view, std::string_view> columns[] = {
                    { DbSchema::kRoleAssignmentsTable, DbSchema::kRoleAssignmentsPersonId },
                    { DbSchema::kRoleAssignmentsTable, DbSchema::kRoleAssignmentsRoleId },
                    { DbSchema::kRolePermissionsTable, DbSchema::kRolePermissionsRoleId },
                };
                for (const auto& [table, column] : columns) {
                    transaction.RunSqlStatement(
                        sql, std::string(table), std::string(column));
                }
            },
        },
    };
}

}  // namespace Migration
