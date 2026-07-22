#include "make_control_database_info.h"

#include "schema_migrations.h"
#include "tenants.h"

namespace DbSchema {

DatabaseInfo MakeControlDatabaseInfo(std::string_view controlDatabaseName) {
    DatabaseInfo databaseInfo(controlDatabaseName);
    MakeTenantsTable(databaseInfo);
    MakeSchemaMigrationsTable(databaseInfo);
    return databaseInfo;
}

}  // namespace DbSchema
