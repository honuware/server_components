#include "table_item_photos.h"

#include "db_schema/photos.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

TableItemPhotos::TableItemPhotos(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

int64_t TableItemPhotos::AddTableItemPhoto(
    Transaction& transaction,
    std::string_view tableName,
    int64_t tableItemId,
    int64_t sourcePhotoId) {
    KeyValueTable kv = {
        { std::string(DbSchema::kTableItemPhotosTableName),
          std::string(tableName) },
        { std::string(DbSchema::kTableItemPhotosTableItemId),
          StringFromInt(tableItemId) },
        { std::string(DbSchema::kTableItemPhotosSourcePhotoId),
          StringFromInt(sourcePhotoId) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction, databaseHelper_, DbSchema::kTableItemPhotos, kv);
}

KeyValueTable TableItemPhotos::GetTableItemPhoto(
    Transaction& transaction,
    std::string_view tableName,
    int64_t tableItemId) const {
    return transaction.RunSqlStatementReturningOneRow(
        "SELECT * FROM table_item_photos "
        "WHERE table_name = $1 AND table_item_id = $2",
        tableName,
        StringFromInt(tableItemId));
}

KeyValueTable TableItemPhotos::GetTableItemPhotoById(
    Transaction& transaction, int64_t id) const {
    return DbCrud::GetRow(
        transaction, databaseHelper_,
        DbSchema::kTableItemPhotos,
        DbSchema::kTableItemPhotosId,
        StringFromInt(id));
}

void TableItemPhotos::UpdateTableItemPhoto(
    Transaction& transaction,
    std::string_view tableName,
    int64_t tableItemId,
    int64_t sourcePhotoId) {
    // Look up the row by unique constraint (table_name, table_item_id)
    KeyValueTable existing = GetTableItemPhoto(transaction, tableName, tableItemId);
    if (existing.empty()) {
        throw std::runtime_error(
            "TableItemPhotos::UpdateTableItemPhoto - no photo found for table/item");
    }
    std::string id = existing.at(std::string(DbSchema::kTableItemPhotosId));

    KeyValueTable kv = {
        { std::string(DbSchema::kTableItemPhotosSourcePhotoId),
          StringFromInt(sourcePhotoId) }
    };
    DbCrud::UpdateRow(
        transaction, databaseHelper_,
        DbSchema::kTableItemPhotos,
        DbSchema::kTableItemPhotosId,
        id,
        kv);
}

void TableItemPhotos::DeleteTableItemPhoto(
    Transaction& transaction,
    std::string_view tableName,
    int64_t tableItemId) {
    transaction.RunSqlStatement(
        "DELETE FROM table_item_photos "
        "WHERE table_name = $1 AND table_item_id = $2",
        tableName,
        StringFromInt(tableItemId));
}

bool TableItemPhotos::HasPhoto(
    Transaction& transaction,
    std::string_view tableName,
    int64_t tableItemId) const {
    KeyValueTable row = GetTableItemPhoto(transaction, tableName, tableItemId);
    return !row.empty();
}

}  // namespace TableHelpers
