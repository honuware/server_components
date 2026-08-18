#include "framework_migrations.h"

#include <string>

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
        // Registering the table in MakeFrameworkTables covers a database built
        // from scratch; it does NOTHING for one that already exists. Without
        // this migration, importing a theme into an existing site fails with
        // `relation "site_assets" does not exist` — and fails at APPLY time,
        // after the dry run has already reported success, because the dry run
        // returns before it touches the table.
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
    };
}

}  // namespace Migration
