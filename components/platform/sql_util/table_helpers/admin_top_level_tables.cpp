#include "sql_util/table_helpers/admin_top_level_tables.h"

#include "sql_util/database_access/database_util.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/database_access/database_metadata.h"
#include "db_schema/admin_top_level_tables.h"

namespace TableHelpers {

AdminTopLevelTables::AdminTopLevelTables(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper)
{
}

void AdminTopLevelTables::AddAdminTopLevelTable(Transaction& transaction, std::string_view tableName)
{
    DbCrud::AddRowToTable(
        transaction,
        databaseHelper_,
        DbSchema::kAdminTopLevelTablesTable,
        { DbPair(DbSchema::kAdminTopLevelTablesName, tableName) });
}

void AdminTopLevelTables::DeleteAdminTopLevelTable(Transaction& transaction, std::string_view tableName)
{
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kAdminTopLevelTablesTable,
        DbSchema::kAdminTopLevelTablesName,
        tableName);
}

bool AdminTopLevelTables::IsTableAdminTopLevel(Transaction& transaction, std::string_view tableName)
{
    StringArray adminTopLevelTables = GetAdminTopLevelTables(transaction);
    return std::find(
        adminTopLevelTables.begin(),
        adminTopLevelTables.end(),
        tableName) != adminTopLevelTables.end();
}

StringArray AdminTopLevelTables::GetAdminTopLevelTables(Transaction& transaction)
{
    DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
        DbMeta::ListColumns(
            transaction, DbSchema::kAdminTopLevelTablesTable);
    std::string sql = DbUtil::GenerateSelectStatementFromTableAndInfo(
        DbSchema::kAdminTopLevelTablesTable, databaseColumnInfoArray);
    StringTable table = transaction.RunSqlStatementReturningStringTable(sql);
    StringArray names;
    for (const StringArray& row : table) {
        // Only has one element in each row
        names.push_back(row[0]);
    }
    return names;
}

} // namespace TableHelpers
