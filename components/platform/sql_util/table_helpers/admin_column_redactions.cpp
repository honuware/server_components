#include "sql_util/table_helpers/admin_column_redactions.h"

#include "sql_util/database_access/database_crud_helpers.h"
#include "db_schema/admin_column_redactions.h"

namespace TableHelpers {

AdminColumnRedactions::AdminColumnRedactions(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper)
{
}

void AdminColumnRedactions::AddAdminColumnRedaction(
    Transaction& transaction,
    std::string_view tableName,
    std::string_view columnName)
{
    KeyValueTable keyValueTable = MakeKeyValueTable({
        { DbSchema::kAdminColumnRedactionsTableName, tableName },
        { DbSchema::kAdminColumnRedactionsColumnName, columnName }
    });
    DbCrud::AddRowToTable(
        transaction,
        databaseHelper_,
        DbSchema::kAdminColumnRedactionsTable,
        keyValueTable);
}

void AdminColumnRedactions::DeleteAdminColumnRedaction(
    Transaction& transaction,
    std::string_view tableName,
    std::string_view columnName)
{
    KeyValueTable keyValueTable = {
        {
            std::string(DbSchema::kAdminColumnRedactionsTableName),
            std::string(tableName)
        },
        {
            std::string(DbSchema::kAdminColumnRedactionsColumnName),
            std::string(columnName)
        }
    };
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kAdminColumnRedactionsTable,
        keyValueTable);
}

bool AdminColumnRedactions::IsRedacted(
    Transaction& transaction,
    std::string_view tableName,
    std::string_view columnName)
{
    KeyValueTable lookup = {
        {
            std::string(DbSchema::kAdminColumnRedactionsTableName),
            std::string(tableName)
        },
        {
            std::string(DbSchema::kAdminColumnRedactionsColumnName),
            std::string(columnName)
        }
    };
    KeyValueTable row = DbCrud::LookupRowByValues(
        transaction,
        databaseHelper_,
        DbSchema::kAdminColumnRedactionsTable,
        lookup);
    return !row.empty();
}

KeyValueTableArray AdminColumnRedactions::GetAdminColumnRedactions(
    Transaction& transaction)
{
    return DbCrud::GetTableRows(
        transaction,
        databaseHelper_,
        DbSchema::kAdminColumnRedactionsTable);
}

ColumnRedactionSet AdminColumnRedactions::LoadColumnRedactionSet(
    Transaction& transaction)
{
    ColumnRedactionSet result;
    KeyValueTableArray rows = GetAdminColumnRedactions(transaction);
    for (const auto& row : rows) {
        result.insert({
            row.at(std::string(DbSchema::kAdminColumnRedactionsTableName)),
            row.at(std::string(DbSchema::kAdminColumnRedactionsColumnName))
        });
    }
    return result;
}

}  // namespace TableHelpers
