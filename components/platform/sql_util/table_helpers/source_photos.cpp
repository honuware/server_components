#include "source_photos.h"

#include "db_schema/photos.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

SourcePhotos::SourcePhotos(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

int64_t SourcePhotos::AddSourcePhoto(
    Transaction& transaction,
    int64_t photoInstanceId) {
    KeyValueTable kv = {
        { std::string(DbSchema::kSourcePhotoPhotoInstanceId),
          StringFromInt(photoInstanceId) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction, databaseHelper_, DbSchema::kSourcePhotos, kv);
}

KeyValueTable SourcePhotos::GetSourcePhoto(
    Transaction& transaction, int64_t id) const {
    return DbCrud::GetRow(
        transaction, databaseHelper_,
        DbSchema::kSourcePhotos,
        DbSchema::kSourcePhotoId,
        StringFromInt(id));
}

KeyValueTable SourcePhotos::GetSourcePhotoWithInstance(
    Transaction& transaction, int64_t id) const {
    return transaction.RunSqlStatementReturningOneRow(
        "SELECT sp.*, pi.photo, pi.type, pi.width, pi.height "
        "FROM source_photos sp "
        "JOIN photo_instances pi ON sp.photo_instance_id = pi.photo_instance_id "
        "WHERE sp.source_photo_id = $1",
        StringFromInt(id));
}

void SourcePhotos::DeleteSourcePhoto(
    Transaction& transaction, int64_t id) {
    // Get the photo_instance_id before deleting
    KeyValueTable sourcePhoto = GetSourcePhoto(transaction, id);
    if (sourcePhoto.empty()) {
        return;
    }
    std::string photoInstanceIdStr =
        sourcePhoto.at(std::string(DbSchema::kSourcePhotoPhotoInstanceId));

    // Delete the source photo (cascade will delete scaled_photos)
    DbCrud::DeleteRow(
        transaction, databaseHelper_,
        DbSchema::kSourcePhotos,
        DbSchema::kSourcePhotoId,
        StringFromInt(id));

    // Delete the associated photo_instance
    DbCrud::DeleteRow(
        transaction, databaseHelper_,
        DbSchema::kPhotoInstances,
        DbSchema::kPhotoInstanceId,
        photoInstanceIdStr);
}

void SourcePhotos::UpdateSourcePhotoTimestamp(
    Transaction& transaction, int64_t id) {
    KeyValueTable kv = {
        { std::string(DbSchema::kSourcePhotoLastUpdatedAtUs),
          std::string(DbSchema::kDatabaseInfoDefaultNow) }
    };
    DbCrud::UpdateRow(
        transaction, databaseHelper_,
        DbSchema::kSourcePhotos,
        DbSchema::kSourcePhotoId,
        StringFromInt(id),
        kv,
        { std::string(DbSchema::kDatabaseInfoDefaultNow) });
}

}  // namespace TableHelpers
