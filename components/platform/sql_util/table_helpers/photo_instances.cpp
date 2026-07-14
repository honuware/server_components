#include "photo_instances.h"

#include "db_schema/photos.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

PhotoInstances::PhotoInstances(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

int64_t PhotoInstances::AddPhotoInstance(
    Transaction& transaction,
    std::string_view photoBytes,
    std::string_view type,
    int width,
    int height) {
    KeyValueTable kv = {
        { std::string(DbSchema::kPhotoInstancePhoto), std::string(photoBytes) },
        { std::string(DbSchema::kPhotoInstanceType), std::string(type) },
        { std::string(DbSchema::kPhotoInstanceWidth), std::to_string(width) },
        { std::string(DbSchema::kPhotoInstanceHeight), std::to_string(height) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction, databaseHelper_, DbSchema::kPhotoInstances, kv);
}

KeyValueTable PhotoInstances::GetPhotoInstance(
    Transaction& transaction, int64_t id) const {
    return DbCrud::GetRow(
        transaction, databaseHelper_,
        DbSchema::kPhotoInstances,
        DbSchema::kPhotoInstanceId,
        StringFromInt(id));
}

void PhotoInstances::UpdatePhotoInstance(
    Transaction& transaction,
    int64_t id,
    std::string_view photoBytes,
    std::string_view type,
    int width,
    int height) {
    KeyValueTable kv = {
        { std::string(DbSchema::kPhotoInstancePhoto), std::string(photoBytes) },
        { std::string(DbSchema::kPhotoInstanceType), std::string(type) },
        { std::string(DbSchema::kPhotoInstanceWidth), std::to_string(width) },
        { std::string(DbSchema::kPhotoInstanceHeight), std::to_string(height) },
        { std::string(DbSchema::kPhotoInstanceLastUpdatedAtUs),
          std::string(DbSchema::kDatabaseInfoDefaultNow) }
    };
    DbCrud::UpdateRow(
        transaction, databaseHelper_,
        DbSchema::kPhotoInstances,
        DbSchema::kPhotoInstanceId,
        StringFromInt(id),
        kv,
        { std::string(DbSchema::kDatabaseInfoDefaultNow) });
}

void PhotoInstances::DeletePhotoInstance(
    Transaction& transaction, int64_t id) {
    DbCrud::DeleteRow(
        transaction, databaseHelper_,
        DbSchema::kPhotoInstances,
        DbSchema::kPhotoInstanceId,
        StringFromInt(id));
}

}  // namespace TableHelpers
