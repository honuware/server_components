#pragma once

#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class ScaledPhotos {
public:
    ScaledPhotos(DatabaseHelper databaseHelper);
    ScaledPhotos(const ScaledPhotos&) = default;
    ScaledPhotos& operator=(const ScaledPhotos&) = default;
    ~ScaledPhotos() = default;

    int64_t AddScaledPhoto(
        Transaction& transaction,
        int64_t sourcePhotoId,
        int64_t photoInstanceId,
        int width,
        int height);
    KeyValueTable GetScaledPhoto(
        Transaction& transaction, int64_t id) const;
    KeyValueTable GetScaledPhotoBySourceAndDimensions(
        Transaction& transaction,
        int64_t sourcePhotoId,
        int width,
        int height) const;
    KeyValueTableArray GetScaledPhotosBySourcePhotoId(
        Transaction& transaction, int64_t sourcePhotoId) const;
    void UpdateLastUsedAtUs(
        Transaction& transaction, int64_t id);
    void DeleteScaledPhoto(
        Transaction& transaction, int64_t id);
    void DeleteScaledPhotosBySourcePhotoId(
        Transaction& transaction, int64_t sourcePhotoId);
    KeyValueTableArray GetScaledPhotosOlderThan(
        Transaction& transaction, int64_t cutoffUs) const;

    // Deletes all scaled photos whose last_used_at_us is strictly less than cutoffUs,
    // along with the underlying photo_instances rows (so the binary blobs are reclaimed).
    // Returns the count of scaled_photos rows deleted.
    int64_t DeleteOlderThan(
        Transaction& transaction, int64_t cutoffUs);
    int64_t GetTotalScaledPhotoStorageBytes(
        Transaction& transaction) const;
    KeyValueTableArray GetScaledPhotosOrderedByLastUsed(
        Transaction& transaction, int limit) const;

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
