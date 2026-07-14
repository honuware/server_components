#include "schema_migrations.h"

namespace DbSchema {

void MakeSchemaMigrationsTable(DatabaseInfo databaseInfo) {
    databaseInfo.AddTable(kSchemaMigrationsTable);
    databaseInfo.AddColumnPrimaryKey(
        kSchemaMigrationsTable,
        kSchemaMigrationsId,
        DB_TYPE_STRING);
    databaseInfo.AddColumnNotNullableWithDefault(
        kSchemaMigrationsTable,
        kSchemaMigrationsAppliedAtUs,
        DB_TYPE_BIGINT,
        kDatabaseInfoDefaultNow);
}

}  // namespace DbSchema
