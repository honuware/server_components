#include "provisioning.h"

#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "business_logic/migration/migration_runner.h"
#include "db_schema/tenants.h"
#include "sql_util/database_access/transaction.h"
#include "sql_util/table_helpers/tenants.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/test_transaction_provider.h"

namespace Tenancy {
namespace {

TableHelpers::TenantRow MakeRow(
    const std::string& siteKey, const std::string& dbName,
    const std::string& status = std::string(DbSchema::kTenantStatusActive)) {
    TableHelpers::TenantRow row;
    row.siteKey = siteKey;
    row.databaseName = dbName;
    row.displayName = siteKey;
    row.status = status;
    row.maxConnections = 1;
    return row;
}

// --- ProvisionTenant: the duplicate-site-key guard throws BEFORE any physical op.

TEST(ProvisioningTest, ProvisionTenantDuplicateSiteKeyThrowsBeforeCreate) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ProvisionTenantDuplicateSiteKeyThrows", [&](Transaction& transaction) {
            TransactionProviderPtr provider =
                MakeTestTransactionProvider(transaction);
            DatabaseHelper db = testDb.GetDatabaseHelper();

            // A tenant with this site key already exists.
            TableHelpers::Tenants tenants(db);
            tenants.Insert(transaction, MakeRow("acme", "tenant_acme"));

            TenantSpec spec;
            spec.siteKey = "acme";           // collides
            spec.databaseName = "tenant_acme_2";
            spec.displayName = "Acme";

            bool createPopulateCalled = false;
            CreatePopulateFn createPopulate =
                [&](DatabaseHelper, DbSchema::DatabaseInfo) {
                    createPopulateCalled = true;
                };

            EXPECT_THROW(
                ProvisionTenant(provider, db, spec, testDb.GetDatabaseInfo(),
                                createPopulate, /*force=*/false),
                std::runtime_error);
            // The throw happened before the create-populate step (and before any
            // physical CREATE DATABASE).
            EXPECT_FALSE(createPopulateCalled);
        });
}

// --- MigrateAllTenants: iterates active tenants in id order via the injected fn.

TEST(ProvisioningTest, MigrateAllTenantsVisitsActiveTenantsInOrder) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "MigrateAllTenantsVisitsActive", [&](Transaction& transaction) {
            TransactionProviderPtr provider =
                MakeTestTransactionProvider(transaction);
            DatabaseHelper db = testDb.GetDatabaseHelper();

            TableHelpers::Tenants tenants(db);
            tenants.Insert(transaction, MakeRow("alpha", "tenant_alpha"));
            tenants.Insert(transaction, MakeRow("beta", "tenant_beta"));
            // A suspended tenant must be skipped.
            tenants.Insert(transaction, MakeRow(
                "gamma", "tenant_gamma",
                std::string(DbSchema::kTenantStatusSuspended)));

            std::vector<std::string> visited;
            TenantMigrateFn migrateTenant =
                [&](const TableHelpers::TenantRow& tenant) {
                    visited.push_back(tenant.siteKey);
                    return Migration::MigrationResult{};
                };

            MigrateAllTenantsResult result =
                MigrateAllTenants(provider, db, migrateTenant);

            const std::vector<std::string> expected{"alpha", "beta"};
            EXPECT_EQ(visited, expected);                    // active only, id order
            EXPECT_EQ(result.migratedSiteKeys, expected);
        });
}

TEST(ProvisioningTest, MigrateAllTenantsAbortsOnFirstFailure) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "MigrateAllTenantsAborts", [&](Transaction& transaction) {
            TransactionProviderPtr provider =
                MakeTestTransactionProvider(transaction);
            DatabaseHelper db = testDb.GetDatabaseHelper();

            TableHelpers::Tenants tenants(db);
            tenants.Insert(transaction, MakeRow("first", "tenant_first"));
            tenants.Insert(transaction, MakeRow("second", "tenant_second"));

            std::vector<std::string> visited;
            TenantMigrateFn migrateTenant =
                [&](const TableHelpers::TenantRow& tenant)
                -> Migration::MigrationResult {
                    visited.push_back(tenant.siteKey);
                    if (tenant.siteKey == "first") {
                        throw Migration::MigrationFailure("app/0001", "boom");
                    }
                    return Migration::MigrationResult{};
                };

            EXPECT_THROW(MigrateAllTenants(provider, db, migrateTenant),
                         Migration::MigrationFailure);
            // Aborted after the first tenant — the second was never touched.
            const std::vector<std::string> expected{"first"};
            EXPECT_EQ(visited, expected);
        });
}

}  // namespace
}  // namespace Tenancy
