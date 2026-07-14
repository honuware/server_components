#include "sql_util/table_helpers/admin_table_friendly_names.h"

#include "sql_util/database_access/database_util.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/database_access/database_metadata.h"
#include "db_schema/admin_table_friendly_names.h"

namespace TableHelpers {

AdminTableFriendlyNames::AdminTableFriendlyNames(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

void AdminTableFriendlyNames::AddAdminTableFriendlyName(
    Transaction& transaction,
    std::string_view tableName,
    std::string_view friendlyName,
    std::string_view description) {
    DbCrud::AddRowToTable(
        transaction,
        databaseHelper_,
        DbSchema::kAdminTableFriendlyNamesTable,
        { 
            DbPair(DbSchema::kAdminTableFriendlyNamesName, tableName),
            DbPair(DbSchema::kAdminTableFriendlyNamesFriendlyName, friendlyName),
            DbPair(DbSchema::kAdminTableFriendlyNamesDescription, description)
        });
}

void AdminTableFriendlyNames::DeleteAdminTableFriendlyName(
    Transaction& transaction,
    std::string_view tableName) {
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kAdminTableFriendlyNamesTable,
        DbSchema::kAdminTableFriendlyNamesName,
        tableName);
}

bool AdminTableFriendlyNames::GetAdminTableFriendlyName(
    Transaction& transaction,
    std::string_view tableName,
    KeyValueTable& keyValueTable) {
    keyValueTable = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kAdminTableFriendlyNamesTable,
        DbSchema::kAdminTableFriendlyNamesName,
        tableName);
    return !keyValueTable.empty();
}

KeyValueTableArray AdminTableFriendlyNames::GetAdminTableFriendlyNames(
    Transaction& transaction) {
    return DbCrud::GetTableRows(
        transaction, databaseHelper_, DbSchema::kAdminTableFriendlyNamesTable);
}

}  // namespace TableHelpers