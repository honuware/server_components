#include "provisioning.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "db_schema/tenants.h"
#include "sql_util/database_access/database_util.h"
#include "sql_util/database_access/production_transaction_provider.h"
#include "sql_util/database_access/transaction.h"
#include "util/destructive_guard.h"
#include "util/logging.h"

namespace Tenancy {

ProvisionTenantResult ProvisionTenant(
    TransactionProviderPtr controlProvider,
    DatabaseHelper controlDatabaseHelper,
    const TenantSpec& spec,
    const DbSchema::DatabaseInfo& tenantDatabaseInfo,
    const CreatePopulateFn& createPopulate,
    bool force) {
    // 1. Idempotency: an existing site_key is an explicit error unless forced.
    //    This runs FIRST, before any physical database op, so a duplicate leaves
    //    nothing behind.
    controlProvider->RunInTransaction([&](Transaction& transaction) {
        TableHelpers::Tenants tenants(controlDatabaseHelper);
        if (tenants.LookupBySiteKey(transaction, spec.siteKey).has_value() &&
            !force) {
            throw std::runtime_error(
                "ProvisionTenant: site_key '" + spec.siteKey +
                "' already exists (pass --force to recreate)");
        }
    });

    // 2. Create (or force-recreate) the tenant database. CREATE DATABASE has no
    //    IF NOT EXISTS, so guard with a pg_database existence check (mirrors
    //    EnsureControlDatabase).
    {
        DatabaseHelper noDatabaseHelper = MakeNoDatabaseHelper();
        noDatabaseHelper.RunInTransaction(
            "ProvisionTenant.createDatabase", [&](Transaction& transaction) {
                std::string count = transaction.RunSqlStatementReturningOneValue(
                    "SELECT COUNT(*) FROM pg_database WHERE datname = $1",
                    spec.databaseName);
                if (count == "0") {
                    DbUtil::MakeDatabase(transaction, spec.databaseName);
                } else if (force) {
                    // Dropping a live tenant database is destructive — gate it.
                    EnsureDestructiveAllowed();
                    DbUtil::RemoveAndCreateDatabase(
                        transaction, spec.databaseName);
                } else {
                    throw std::runtime_error(
                        "ProvisionTenant: database '" + spec.databaseName +
                        "' already exists");
                }
            });
    }

    // 3. Install the schema + seed data in the new tenant database.
    DatabaseHelper tenantHelper = MakeProductionDatabaseHelper(spec.databaseName);
    createPopulate(tenantHelper, tenantDatabaseInfo);

    // 4. Record the tenant in the control database.
    ProvisionTenantResult result;
    controlProvider->RunInTransaction([&](Transaction& transaction) {
        TableHelpers::Tenants tenants(controlDatabaseHelper);
        std::optional<TableHelpers::TenantRow> existing =
            tenants.LookupBySiteKey(transaction, spec.siteKey);
        if (existing.has_value()) {
            // --force path: the row is already present; keep it as-is.
            result.tenantId = existing->id;
            result.created = false;
        } else {
            TableHelpers::TenantRow row;
            row.siteKey = spec.siteKey;
            row.databaseName = spec.databaseName;
            row.displayName = spec.displayName;
            row.status = std::string(DbSchema::kTenantStatusActive);
            row.maxConnections = spec.maxConnections > 0 ? spec.maxConnections : 1;
            result.tenantId = tenants.Insert(transaction, row);
            result.created = true;
        }
    });

    LogInfo() << "[provision_tenant] event=done site=" << spec.siteKey
              << " db=" << spec.databaseName << " tenant_id=" << result.tenantId
              << " created=" << (result.created ? "true" : "false") << "\n";
    return result;
}

MigrateAllTenantsResult MigrateAllTenants(
    TransactionProviderPtr controlProvider,
    DatabaseHelper controlDatabaseHelper,
    const TenantMigrateFn& migrateTenant) {
    std::vector<TableHelpers::TenantRow> activeTenants;
    controlProvider->RunInTransaction([&](Transaction& transaction) {
        TableHelpers::Tenants tenants(controlDatabaseHelper);
        activeTenants = tenants.ListActive(transaction);
    });

    MigrateAllTenantsResult result;
    for (const TableHelpers::TenantRow& tenant : activeTenants) {
        LogInfo() << "[migrate_tenants] event=start site=" << tenant.siteKey
                  << " db=" << tenant.databaseName << "\n";
        // Aborts the whole run on the first tenant failure (Q6): whatever
        // migrateTenant throws propagates out, and no later tenant is touched.
        Migration::MigrationResult migrationResult = migrateTenant(tenant);
        LogInfo() << "[migrate_tenants] event=done site=" << tenant.siteKey
                  << " applied=" << migrationResult.applied.size()
                  << " skipped=" << migrationResult.skipped.size() << "\n";
        result.migratedSiteKeys.push_back(tenant.siteKey);
    }
    return result;
}

TenantMigrateFn MakeProductionTenantMigrateFn(
    const std::vector<Migration::Migration>& tenantMigrations) {
    return [tenantMigrations](const TableHelpers::TenantRow& tenant) {
        DatabaseHelper tenantHelper =
            MakeProductionDatabaseHelper(tenant.databaseName);
        TransactionProviderPtr tenantProvider =
            MakeProductionTransactionProvider(tenantHelper);
        Migration::MigrationRunner runner(tenantHelper);
        return runner.ApplyPending(*tenantProvider, tenantMigrations);
    };
}

}  // namespace Tenancy
