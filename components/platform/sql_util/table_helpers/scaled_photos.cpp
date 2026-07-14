#include "scaled_photos.h"

#include "db_schema/photos.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

ScaledPhotos::ScaledPhotos(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

int64_t ScaledPhotos::AddScaledPhoto(
    Transaction& transaction,
    int64_t sourcePhotoId,
    int64_t photoInstanceId,
    int width,
    int height) {
    KeyValueTable kv = {
        { std::string(DbSchema::kScaledPhotoSourcePhotoId),
          StringFromInt(sourcePhotoId) },
        { std::string(DbSchema::kScaledPhotoPhotoInstanceId),
          StringFromInt(photoInstanceId) },
        { std::string(DbSchema::kScaledPhotoWidth), std::to_string(width) },
        { std::string(DbSchema::kScaledPhotoHeight), std::to_string(height) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction, databaseHelper_, DbSchema::kScaledPhotos, kv);
}

KeyValueTable ScaledPhotos::GetScaledPhoto(
    Transaction& transaction, int64_t id) const {
    return DbCrud::GetRow(
        transaction, databaseHelper_,
        DbSchema::kScaledPhotos,
        DbSchema::kScaledPhotoId,
        StringFromInt(id));
}

KeyValueTable ScaledPhotos::GetScaledPhotoBySourceAndDimensions(
    Transaction& transaction,
    int64_t sourcePhotoId,
    int width,
    int height) const {
    KeyValueTableArray results =
        transaction.RunSqlStatementReturningKeyValueTableArray(
            "SELECT * FROM scaled_photos "
            "WHERE source_photo_id = $1 AND width = $2 AND height = $3",
            StringFromInt(sourcePhotoId),
            std::to_string(width),
            std::to_string(height));
    if (results.empty()) {
        return {};
    }
    return results[0];
}

KeyValueTableArray ScaledPhotos::GetScaledPhotosBySourcePhotoId(
    Transaction& transaction, int64_t sourcePhotoId) const {
    return transaction.RunSqlStatementReturningKeyValueTableArray(
        "SELECT * FROM scaled_photos WHERE source_photo_id = $1",
        StringFromInt(sourcePhotoId));
}

void ScaledPhotos::UpdateLastUsedAtUs(
    Transaction& transaction, int64_t id) {
    KeyValueTable kv = {
        { std::string(DbSchema::kScaledPhotoLastUsedAtUs),
          std::string(DbSchema::kDatabaseInfoDefaultNow) }
    };
    DbCrud::UpdateRow(
        transaction, databaseHelper_,
        DbSchema::kScaledPhotos,
        DbSchema::kScaledPhotoId,
        StringFromInt(id),
        kv,
        { std::string(DbSchema::kDatabaseInfoDefaultNow) });
}

void ScaledPhotos::DeleteScaledPhoto(
    Transaction& transaction, int64_t id) {
    // Get the photo_instance_id before deleting
    KeyValueTable scaledPhoto = GetScaledPhoto(transaction, id);
    if (scaledPhoto.empty()) {
        return;
    }
    std::string photoInstanceIdStr =
        scaledPhoto.at(std::string(DbSchema::kScaledPhotoPhotoInstanceId));

    DbCrud::DeleteRow(
        transaction, databaseHelper_,
        DbSchema::kScaledPhotos,
        DbSchema::kScaledPhotoId,
        StringFromInt(id));

    // Delete the associated photo_instance
    DbCrud::DeleteRow(
        transaction, databaseHelper_,
        DbSchema::kPhotoInstances,
        DbSchema::kPhotoInstanceId,
        photoInstanceIdStr);
}

void ScaledPhotos::DeleteScaledPhotosBySourcePhotoId(
    Transaction& transaction, int64_t sourcePhotoId) {
    // Get all scaled photos for this source to find their photo_instance_ids
    KeyValueTableArray scaledPhotos =
        GetScaledPhotosBySourcePhotoId(transaction, sourcePhotoId);

    // Delete all scaled photo rows
    transaction.RunSqlStatement(
        "DELETE FROM scaled_photos WHERE source_photo_id = $1",
        StringFromInt(sourcePhotoId));

    // Delete associated photo_instances
    for (const auto& sp : scaledPhotos) {
        std::string photoInstanceIdStr =
            sp.at(std::string(DbSchema::kScaledPhotoPhotoInstanceId));
        DbCrud::DeleteRow(
            transaction, databaseHelper_,
            DbSchema::kPhotoInstances,
            DbSchema::kPhotoInstanceId,
            photoInstanceIdStr);
    }
}

KeyValueTableArray ScaledPhotos::GetScaledPhotosOlderThan(
    Transaction& transaction, int64_t cutoffUs) const {
    return transaction.RunSqlStatementReturningKeyValueTableArray(
        "SELECT * FROM scaled_photos WHERE last_used_at_us < $1 "
        "ORDER BY last_used_at_us ASC",
        StringFromInt(cutoffUs));
}

int64_t ScaledPhotos::DeleteOlderThan(
    Transaction& transaction, int64_t cutoffUs) {
    // Single DELETE returns each deleted row's photo_instance_id so we can
    // cascade to the binary-blob table afterwards. Mirrors the cascading
    // behavior of DeleteScaledPhoto and DeleteScaledPhotosBySourcePhotoId.
    KeyValueTableArray deletedRows =
        transaction.RunSqlStatementReturningKeyValueTableArray(
            "DELETE FROM scaled_photos WHERE last_used_at_us < $1 "
            "RETURNING photo_instance_id",
            StringFromInt(cutoffUs));

    for (const auto& row : deletedRows) {
        DbCrud::DeleteRow(
            transaction, databaseHelper_,
            DbSchema::kPhotoInstances,
            DbSchema::kPhotoInstanceId,
            row.at(std::string(DbSchema::kScaledPhotoPhotoInstanceId)));
    }

    return static_cast<int64_t>(deletedRows.size());
}

int64_t ScaledPhotos::GetTotalScaledPhotoStorageBytes(
    Transaction& transaction) const {
    std::string result = transaction.RunSqlStatementReturningOneValue(
        "SELECT COALESCE(SUM(LENGTH(pi.photo)), 0) "
        "FROM scaled_photos sp "
        "JOIN photo_instances pi ON sp.photo_instance_id = pi.photo_instance_id");
    return std::stoll(result);
}

KeyValueTableArray ScaledPhotos::GetScaledPhotosOrderedByLastUsed(
    Transaction& transaction, int limit) const {
    return transaction.RunSqlStatementReturningKeyValueTableArray(
        "SELECT * FROM scaled_photos ORDER BY last_used_at_us ASC LIMIT $1",
        std::to_string(limit));
}

}  // namespace TableHelpers
