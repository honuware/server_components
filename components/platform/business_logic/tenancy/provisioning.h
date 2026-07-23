#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "business_logic/migration/migration_runner.h"   // Migration::Migration / MigrationResult
#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/transaction_provider.h"
#include "sql_util/schema/database_info.h"
#include "sql_util/table_helpers/tenants.h"               // TableHelpers::TenantRow

namespace Tenancy {

// The identity + sizing for a tenant to provision. maxConnections <= 0 falls back
// to the tenants column default (1).
struct TenantSpec {
    std::string siteKey;
    std::string databaseName;
    std::string displayName;
    int64_t maxConnections = 1;
};

// Result of provisioning: the control-row id + whether a NEW row was created
// (false when --force re-provisioned an existing tenant, keeping its row).
struct ProvisionTenantResult {
    int64_t tenantId = 0;
    bool created = false;
};

// The app-supplied create+populate callable: given a freshly-created empty tenant
// database (helper) and its composed schema, install the schema + seed data.
// knottyyoga passes CreateSchemaAndPopulate (create_database.h); a framework-only
// consumer passes its own equivalent.
using CreatePopulateFn =
    std::function<void(DatabaseHelper, DbSchema::DatabaseInfo)>;

// Stands up a new tenant end to end (tenancy plan Phase 5.2). The CALLER must have
// already ensured the control database exists (EnsureControlDatabase) and pass its
// provider + helper here — so this function is pure control-plane logic + the
// physical create, and its control-row behavior is unit-testable over a test
// schema. Steps:
//   1. Reject an existing site_key (idempotency) unless `force`.
//   2. Create the tenant database (guarded pg_database check; `force` drops +
//      recreates behind the destructive gate HONUWARE_ALLOW_DESTRUCTIVE).
//   3. Run `createPopulate(tenantHelper, tenantDatabaseInfo)` to install schema +
//      seed data.
//   4. Insert the `tenants` control row (kept as-is on the force path).
// Throws std::runtime_error on a duplicate site_key / existing database (no force)
// / destructive-not-allowed. A duplicate-key throw happens BEFORE any physical op.
ProvisionTenantResult ProvisionTenant(
    TransactionProviderPtr controlProvider,
    DatabaseHelper controlDatabaseHelper,
    const TenantSpec& spec,
    const DbSchema::DatabaseInfo& tenantDatabaseInfo,
    const CreatePopulateFn& createPopulate,
    bool force = false);

// Result of a multi-tenant migration run.
struct MigrateAllTenantsResult {
    std::vector<std::string> migratedSiteKeys;   // active tenants migrated, in order
};

// The per-tenant apply step, injectable so the iteration/abort logic is testable
// without touching real databases. Prod binds it to a real MigrationRunner over
// the tenant's own provider (MakeProductionTenantMigrateFn).
using TenantMigrateFn =
    std::function<Migration::MigrationResult(const TableHelpers::TenantRow&)>;

// Applies migrations to EVERY active tenant (tenancy plan Phase 5.3): iterates
// Tenants::ListActive from the control DB and calls `migrateTenant` for each, in
// id order. Aborts on the FIRST tenant failure (Q6) — whatever `migrateTenant`
// throws (e.g. Migration::MigrationFailure) propagates, and no later tenant is
// touched. Already-migrated tenants are skipped by the runner via each tenant's
// schema_migrations, so re-runs are safe.
MigrateAllTenantsResult MigrateAllTenants(
    TransactionProviderPtr controlProvider,
    DatabaseHelper controlDatabaseHelper,
    const TenantMigrateFn& migrateTenant);

// The production per-tenant migrate step: opens a real per-tenant provider over
// the tenant's own database and applies `tenantMigrations` via a MigrationRunner.
// The composed (namespaced framework++app) list is idempotent, so it is safe to
// apply to every tenant.
TenantMigrateFn MakeProductionTenantMigrateFn(
    const std::vector<Migration::Migration>& tenantMigrations);

}  // namespace Tenancy
