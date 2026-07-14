#include "allowed_tables.h"

#include "sql_util/database_access/database_util.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/database_access/database_metadata.h"
#include "db_schema/allowed_tables.h"

namespace TableHelpers {

AllowedTables::AllowedTables(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper)
{
}

void AllowedTables::AddAllowedTable(
    Transaction& transaction, std::string_view tableName) {
    DbCrud::AddRowToTable(
        transaction,
        databaseHelper_, 
        DbSchema::kAllowedTablesTable, 
        {DbPair(DbSchema::kAllowedTablesName, tableName)});
}

void AllowedTables::DeleteAllowedTable(
    Transaction& transaction, std::string_view tableName) {
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kAllowedTablesTable, 
        DbSchema::kAllowedTablesName, 
        tableName);
}

bool AllowedTables::IsTableAllowed(
    Transaction& transaction, std::string_view tableName) {
    StringArray allowedTables = GetAllowedTables(transaction);
    return std::find(
        allowedTables.begin(),
        allowedTables.end(),
        tableName) != allowedTables.end();
}

StringArray AllowedTables::GetAllowedTables(Transaction& transaction) {
    KeyValueTableArray keyValueTableArray = DbCrud::GetTableRows(
        transaction, databaseHelper_, DbSchema::kAllowedTablesTable);
    return StringArrayFromKeyValueTableArrayColumn(keyValueTableArray);
}

} // namespace TableHelpers
