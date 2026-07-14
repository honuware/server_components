#include "sql_util/table_helpers/admin_column_data_info.h"

#include "sql_util/database_access/database_util.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/database_access/database_metadata.h"
#include "db_schema/admin_column_data_info.h"

namespace TableHelpers {

AdminColumnDataInfo::AdminColumnDataInfo(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper)
{
}

void AdminColumnDataInfo::AddAdminColumnDataInfo(
    Transaction& transaction,
    std::string_view tableName,
    std::string_view columnName,
    std::string_view label,
    std::string_view hint /* = "" */,
    std::string_view placeHolder /* = "" */,
    std::string_view regex /* = "" */,
    std::string_view htmlInputType /* = "" */,
    std::string_view required /* = "" */,
    std::string_view maxLength /* = "" */,
    std::string_view defaultValue /* = "" */,
    std::string_view rows /* = "" */,
    std::string_view hidden /* = "" */,
    std::string_view readonly_ /* = "" */)
{
    KeyValueTable keyValueTable = MakeKeyValueTable(
        {
            { DbSchema::kAdminColumnDataInfoTableName, tableName },
            { DbSchema::kAdminColumnDataInfoColumnName, columnName },
            { DbSchema::kAdminColumnDataInfoLabel, label }
        });
    if (!hint.empty()) 
        AddToKeyValueTable(
            keyValueTable, DbSchema::kAdminColumnDataInfoHint, hint);
    if (!placeHolder.empty()) 
        AddToKeyValueTable(
            keyValueTable, DbSchema::kAdminColumnDataInfoPlaceHolder, placeHolder);
    if (!regex.empty()) 
        AddToKeyValueTable(
            keyValueTable, DbSchema::kAdminColumnDataInfoRegex, regex);
    if (!htmlInputType.empty()) 
        AddToKeyValueTable(
            keyValueTable, DbSchema::kAdminColumnDataInfoHtmlInputType, htmlInputType);
    if (!required.empty()) 
        AddToKeyValueTable(
            keyValueTable, DbSchema::kAdminColumnDataInfoRequired, required);
    if (!maxLength.empty()) 
        AddToKeyValueTable(
            keyValueTable, DbSchema::kAdminColumnDataInfoMaxLength, maxLength);
    if (!defaultValue.empty()) 
        AddToKeyValueTable(
            keyValueTable, DbSchema::kAdminColumnDataInfoDefaultValue, defaultValue);
    if (!rows.empty())
        AddToKeyValueTable(
            keyValueTable, DbSchema::kAdminColumnDataInfoRows, rows);
    if (!hidden.empty())
        AddToKeyValueTable(
            keyValueTable, DbSchema::kAdminColumnDataInfoHidden, hidden);
    if (!readonly_.empty())
        AddToKeyValueTable(
            keyValueTable, DbSchema::kAdminColumnDataInfoReadonly, readonly_);
    DbCrud::AddRowToTable(
        transaction,
        databaseHelper_,
        DbSchema::kAdminColumnDataInfoTable,
        keyValueTable);
}

void AdminColumnDataInfo::DeleteAdminColumnDataInfo(
    Transaction& transaction,
    std::string_view table_name, 
    std::string_view columnName)
{
    KeyValueTable keyValueTable = {
        {
            static_cast<std::string>(
                DbSchema::kAdminColumnDataInfoTableName),
            static_cast<std::string>(table_name)
        },
        {
            static_cast<std::string>(
                DbSchema::kAdminColumnDataInfoColumnName),
            static_cast<std::string>(columnName)
        }
    };
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kAdminColumnDataInfoTable,
        keyValueTable);
}

bool AdminColumnDataInfo::GetAdminColumnDataInfo(
    Transaction& transaction,
    std::string_view table_name,
    std::string_view columnName, 
    KeyValueTable& keyValueTable)
{
    KeyValueTable keyValueTableLookup = {
        {
            std::string(DbSchema::kAdminColumnDataInfoTableName),
            std::string(table_name)
        },
        {
            std::string(DbSchema::kAdminColumnDataInfoColumnName),
            std::string(columnName)
        }
    };
    keyValueTable = DbCrud::LookupRowByValues(
        transaction,
        databaseHelper_,
        DbSchema::kAdminColumnDataInfoTable,
        keyValueTableLookup);
    return !keyValueTable.empty();
}

KeyValueTableArray AdminColumnDataInfo::GetAdminColumnDataInfos(Transaction& transaction)
{
    KeyValueTableArray keyValueTableArray;
    DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
        DbMeta::ListColumns(
            transaction, DbSchema::kAdminColumnDataInfoTable);
    std::string sql = DbUtil::GenerateSelectStatementFromTableAndInfo(
        DbSchema::kAdminColumnDataInfoTable, databaseColumnInfoArray);
    return DbCrud::GetTableRows(
        transaction, databaseHelper_, DbSchema::kAdminColumnDataInfoTable);
}

} // namespace TableHelpers
