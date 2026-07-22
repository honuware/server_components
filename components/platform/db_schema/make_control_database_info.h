#pragma once

#include <string_view>

#include "sql_util/schema/database_info.h"

namespace DbSchema {

// Assembles the schema for the control database (default name honuware_control):
// the `tenants` registry plus `schema_migrations` for the control DB's own
// evolution, and nothing else. Deliberately separate from MakeFrameworkTables —
// a tenant database must not carry a `tenants` table, and the control database
// must not carry the framework's business tables.
DatabaseInfo MakeControlDatabaseInfo(std::string_view controlDatabaseName);

}  // namespace DbSchema
