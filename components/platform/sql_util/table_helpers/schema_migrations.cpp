#include "schema_migrations.h"

#include <string>

#include "db_schema/schema_migrations.h"
#include "sql_util/database_access/database_crud_helpers.h"

namespace TableHelpers {

SchemaMigrations::SchemaMigrations(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

bool SchemaMigrations::IsApplied(
    Transaction& transaction, std::string_view id) const {
    KeyValueTable row = DbCrud::LookupRowByValue(
        transaction, databaseHelper_,
        DbSchema::kSchemaMigrationsTable,
        DbSchema::kSchemaMigrationsId,
        id);
    return !row.empty();
}

StringArray SchemaMigrations::ListAppliedIds(Transaction& transaction) const {
    // Multi-column ORDER BY (applied_at_us, then id as tiebreak) is one of
    // the documented DbCrud-can't-do-it cases, so direct SQL is OK here —
    // and it stays in the table helper, not in business logic.
    KeyValueTableArray rows = transaction.RunSqlStatementReturningKeyValueTableArray(
        "SELECT id FROM schema_migrations "
        "ORDER BY applied_at_us ASC, id ASC");
    StringArray ids;
    ids.reserve(rows.size());
    for (const auto& row : rows) {
        ids.push_back(row.at(std::string(DbSchema::kSchemaMigrationsId)));
    }
    return ids;
}

void SchemaMigrations::RecordApplied(
    Transaction& transaction, std::string_view id) {
    KeyValueTable kv = {
        { std::string(DbSchema::kSchemaMigrationsId), std::string(id) }
    };
    DbCrud::AddRowToTable(
        transaction, databaseHelper_,
        DbSchema::kSchemaMigrationsTable, kv);
}

}  // namespace TableHelpers
