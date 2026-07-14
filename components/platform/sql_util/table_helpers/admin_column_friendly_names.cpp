#include "sql_util/table_helpers/admin_column_friendly_names.h"

#include "sql_util/database_access/database_util.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/database_access/database_metadata.h"
#include "db_schema/admin_column_friendly_names.h"

namespace TableHelpers {

AdminColumnFriendlyNames::AdminColumnFriendlyNames(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper)
{
}

void AdminColumnFriendlyNames::AddAdminColumnFriendlyName(
    Transaction& transaction,
    std::string_view tableName,
    std::string_view columnName,
    std::string_view friendlyName)
{
    DbCrud::AddRowToTable(
        transaction,
        databaseHelper_,
        DbSchema::kAdminColumnFriendlyNamesTable,
        {
            DbPair(DbSchema::kAdminColumnFriendlyNamesTableName, tableName),
            DbPair(DbSchema::kAdminColumnFriendlyNamesColumnName, columnName),
            DbPair(DbSchema::kAdminColumnFriendlyNamesFriendlyName, friendlyName)
        });
}

void AdminColumnFriendlyNames::DeleteAdminColumnFriendlyName(
    Transaction& transaction,
    std::string_view table_name,
    std::string_view columnName)
{
    KeyValueTable keyValueTable = {
        {
            static_cast<std::string>(
                DbSchema::kAdminColumnFriendlyNamesTableName), 
            static_cast<std::string>(table_name)
        },
        {
            static_cast<std::string>(
                DbSchema::kAdminColumnFriendlyNamesColumnName), 
            static_cast<std::string>(columnName)
        }
    };
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kAdminColumnFriendlyNamesTable,
        keyValueTable);
}

std::string AdminColumnFriendlyNames::GetAdminColumnFriendlyName(
    Transaction& transaction,
    std::string_view table_name,
    std::string_view columnName)
{
    KeyValueTable keyValueTableLookup = {
        {
            std::string(DbSchema::kAdminColumnFriendlyNamesTableName), 
            std::string(table_name)
        },
        {
            std::string(DbSchema::kAdminColumnFriendlyNamesColumnName),
            std::string(columnName)
        }
    };
    KeyValueTable keyValueTable = DbCrud::LookupRowByValues(
        transaction,
        databaseHelper_,
        DbSchema::kAdminColumnFriendlyNamesTable,
        keyValueTableLookup);
    return keyValueTable[std::string(
        DbSchema::kAdminColumnFriendlyNamesFriendlyName)];
}

KeyValueTableArray AdminColumnFriendlyNames::GetAdminColumnFriendlyNames(
    Transaction& transaction)
{
    return DbCrud::GetTableRows(
        transaction,
        databaseHelper_,
        DbSchema::kAdminColumnFriendlyNamesTable);
}

}  // namespace TableHelpers {