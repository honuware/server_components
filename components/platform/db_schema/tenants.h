#pragma once

#include <string_view>

#include "sql_util/schema/database_info.h"

namespace DbSchema {

// The `tenants` control table: maps a CloudFront site key (the value sent in the
// X-Honuware-Site header) to a tenant's database and display metadata. It lives
// ONLY in the control database (honuware_control) — it is assembled by
// MakeControlDatabaseInfo, never by MakeFrameworkTables, because tenant
// databases must not carry a `tenants` table.
inline constexpr std::string_view kTenantsTable = "tenants";
inline constexpr std::string_view kTenantsId = "id";
inline constexpr std::string_view kTenantsSiteKey = "site_key";
inline constexpr std::string_view kTenantsDatabaseName = "database_name";
inline constexpr std::string_view kTenantsDisplayName = "display_name";
inline constexpr std::string_view kTenantsStatus = "status";
inline constexpr std::string_view kTenantsMaxConnections = "max_connections";
inline constexpr std::string_view kTenantsCreatedUs = "created_us";
inline constexpr std::string_view kTenantsUpdatedUs = "updated_us";

// Allowed values for the `status` column.
inline constexpr std::string_view kTenantStatusActive = "active";
inline constexpr std::string_view kTenantStatusSuspended = "suspended";

void MakeTenantsTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema
