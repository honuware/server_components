#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

// Tracks which database migrations have been applied. Owned by
// `Migration::MigrationRunner` (see `business_logic/migration/`). Operators
// inspect via `SELECT * FROM schema_migrations ORDER BY applied_at_us`;
// nothing else reads it.
//
// Row shape:
//   id            TEXT primary key, e.g. "0001_baseline", "0002_add_..."
//   applied_at_us BIGINT NOT NULL DEFAULT now_us()
//
// `applied_at_us` follows the codebase convention of microseconds-since-
// epoch BIGINT (matches admin_alerts.created_at, bookings.cancelled_us,
// etc.) rather than TIMESTAMPTZ. The plan doc loosely said "TIMESTAMPTZ"
// but consistency with every other timestamp column in the schema wins.
inline constexpr std::string_view kSchemaMigrationsTable = "schema_migrations";
inline constexpr std::string_view kSchemaMigrationsId = "id";
inline constexpr std::string_view kSchemaMigrationsAppliedAtUs = "applied_at_us";

void MakeSchemaMigrationsTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema
