#include "control_database.h"

#include <string>
#include <string_view>

#include "db_schema/make_control_database_info.h"
#include "sql_util/database_access/database_util.h"
#include "sql_util/database_access/db_and_table_operations.h"
#include "sql_util/schema/database_info.h"
#include "sql_util/stored_procedures/now_us.h"
#include "util/env.h"

namespace Tenancy {
namespace {

constexpr std::string_view kDefaultControlDatabaseName = "honuware_control";

}  // namespace

std::string ControlDatabaseName() {
    const char* value = Util::GetEnvWithFallback(
        "HONUWARE_CONTROL_DB_NAME", "KNOTTYYOGA_CONTROL_DB_NAME");
    if (value == nullptr || value[0] == '\0') {
        return std::string(kDefaultControlDatabaseName);
    }
    return std::string(value);
}

DatabaseHelper MakeControlDatabaseHelper() {
    return MakeProductionDatabaseHelper(ControlDatabaseName());
}

void EnsureControlDatabase() {
    const std::string controlDatabaseName = ControlDatabaseName();
    DbSchema::DatabaseInfo controlInfo =
        DbSchema::MakeControlDatabaseInfo(controlDatabaseName);

    // 1. Create the database if absent. Non-destructive — we must never drop a
    //    control database that already holds tenant rows. CREATE DATABASE has no
    //    IF NOT EXISTS form, so guard it with a pg_database existence check.
    DatabaseHelper noDatabaseHelper = MakeNoDatabaseHelper();
    noDatabaseHelper.RunInTransaction(
        "EnsureControlDatabase.createIfAbsent", [&](Transaction& transaction) {
            std::string count = transaction.RunSqlStatementReturningOneValue(
                "SELECT COUNT(*) FROM pg_database WHERE datname = $1",
                controlDatabaseName);
            if (count == "0") {
                DbUtil::MakeDatabase(transaction, controlDatabaseName);
            }
        });

    // 2. Create / refresh its tables. Idempotent via CREATE TABLE IF NOT EXISTS.
    DatabaseHelper controlHelper =
        MakeProductionDatabaseHelper(controlDatabaseName);
    controlHelper.RunInTransaction(
        "EnsureControlDatabase.createTables", [&](Transaction& transaction) {
            // now_us() is the DEFAULT for the tenants / schema_migrations
            // timestamp columns, so it must exist before the tables are created.
            StoredProcedures::CreateNowUs(transaction);
            const bool previousMode = DbOps::IsIfNotExistsMode();
            DbOps::SetIfNotExistsMode(true);
            for (const std::string& tableName : controlInfo.GetAllTables()) {
                DbOps::CreateTable(transaction, controlInfo, tableName);
            }
            DbOps::SetIfNotExistsMode(previousMode);
        });
}

}  // namespace Tenancy
