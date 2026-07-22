#include "tenants.h"

namespace DbSchema {

void MakeTenantsTable(DatabaseInfo databaseInfo) {
    databaseInfo.AddTable(kTenantsTable);
    databaseInfo.AddColumnPrimaryKey(kTenantsTable, kTenantsId, DB_TYPE_BIGSERIAL);
    databaseInfo.AddColumnUnique(kTenantsTable, kTenantsSiteKey, DB_TYPE_STRING);
    databaseInfo.AddColumnUnique(kTenantsTable, kTenantsDatabaseName, DB_TYPE_STRING);
    databaseInfo.AddColumnSimple(kTenantsTable, kTenantsDisplayName, DB_TYPE_STRING);
    // The default value is inserted verbatim into the generated DDL, so a string
    // literal must carry its own SQL quotes: DEFAULT 'active'.
    databaseInfo.AddColumnNotNullableWithDefault(
        kTenantsTable, kTenantsStatus, DB_TYPE_STRING, "'active'");
    // Per-tenant connection-pool bound (Phase 2). Defaults to 1 = today's
    // single-connection-per-tenant behavior.
    databaseInfo.AddColumnNotNullableWithDefault(
        kTenantsTable, kTenantsMaxConnections, DB_TYPE_BIGINT, "1");
    databaseInfo.AddColumnNotNullableWithDefault(
        kTenantsTable, kTenantsCreatedUs, DB_TYPE_BIGINT, kDatabaseInfoDefaultNow);
    databaseInfo.AddColumnNotNullableWithDefault(
        kTenantsTable, kTenantsUpdatedUs, DB_TYPE_BIGINT, kDatabaseInfoDefaultNow);
}

}  // namespace DbSchema
