#include "sql_util/table_helpers/admin_nested_tables.h"

#include "sql_util/database_access/database_util.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/database_access/database_metadata.h"
#include "db_schema/admin_nested_tables.h"

namespace TableHelpers {

AdminNestedTables::AdminNestedTables(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper)
{
}

void AdminNestedTables::AddAdminNestedTable(Transaction& transaction, std::string_view tableName)
{
    DbCrud::AddRowToTable(
        transaction,
        databaseHelper_,
        DbSchema::kAdminNestedTablesTable,
        { DbPair(DbSchema::kAdminNestedTablesName, tableName) });
}

void AdminNestedTables::DeleteAdminNestedTable(Transaction& transaction, std::string_view tableName)
{
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kAdminNestedTablesTable,
        DbSchema::kAdminNestedTablesName,
        tableName);
}

bool AdminNestedTables::IsNestedTable(Transaction& transaction, std::string_view tableName)
{
    StringArray adminNestedTables = GetAdminNestedTables(transaction);
    return std::find(
        adminNestedTables.begin(),
        adminNestedTables.end(),
        tableName) != adminNestedTables.end();
}

StringArray AdminNestedTables::GetAdminNestedTables(Transaction& transaction)
{
    DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
        DbMeta::ListColumns(
            transaction, DbSchema::kAdminNestedTablesTable);
    std::string sql = DbUtil::GenerateSelectStatementFromTableAndInfo(
        DbSchema::kAdminNestedTablesTable, databaseColumnInfoArray);
    StringTable table = transaction.RunSqlStatementReturningStringTable(sql);
    StringArray names;
    for (const StringArray& row : table) {
        // Only has one element in each row
        names.push_back(row[0]);
    }
    return names;
}

} // namespace TableHelpers
