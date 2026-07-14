#include "photo_support_tables.h"

#include "db_schema/photos.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

PhotoSupportTables::PhotoSupportTables(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

void PhotoSupportTables::AddPhotoSupportTable(
    Transaction& transaction, std::string_view tableName) {
    DbCrud::AddRowToTable(
        transaction, databaseHelper_,
        DbSchema::kPhotoSupportTables,
        { DbPair(DbSchema::kPhotoSupportTablesTableName, tableName) });
}

bool PhotoSupportTables::IsPhotoSupportedForTable(
    Transaction& transaction, std::string_view tableName) const {
    KeyValueTable row = DbCrud::GetRow(
        transaction, databaseHelper_,
        DbSchema::kPhotoSupportTables,
        DbSchema::kPhotoSupportTablesTableName,
        tableName);
    return !row.empty();
}

KeyValueTableArray PhotoSupportTables::GetAllPhotoSupportTables(
    Transaction& transaction) const {
    return DbCrud::GetTableRows(
        transaction, databaseHelper_, DbSchema::kPhotoSupportTables);
}

void PhotoSupportTables::DeletePhotoSupportTable(
    Transaction& transaction, std::string_view tableName) {
    DbCrud::DeleteRow(
        transaction, databaseHelper_,
        DbSchema::kPhotoSupportTables,
        DbSchema::kPhotoSupportTablesTableName,
        tableName);
}

}  // namespace TableHelpers
