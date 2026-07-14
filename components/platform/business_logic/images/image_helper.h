#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "image_info.h"
#include "util/secrets/secrets_helper.h"
#include "sql_util/database_access/database_helper.h"
#include "sql_util/table_helpers/photo_instances.h"
#include "sql_util/table_helpers/source_photos.h"
#include "sql_util/table_helpers/scaled_photos.h"
#include "sql_util/table_helpers/photo_support_tables.h"
#include "sql_util/table_helpers/table_item_photos.h"

namespace Images {

class ImageHelper {
public:
    ImageHelper(DatabaseHelper databaseHelper);
    ImageHelper(DatabaseHelper databaseHelper,
                Secrets::SecretsHelperPtr secretsHelper);
    ImageHelper() = delete;
    ImageHelper(const ImageHelper&) = default;
    ImageHelper& operator=(const ImageHelper&) = default;
    ~ImageHelper() = default;

    UploadResult UploadAndAssociatePhoto(
        Transaction& transaction,
        std::string_view tableName,
        int64_t tableItemId,
        const std::vector<char>& imageBytes,
        std::string_view imageType);

    bool DeletePhotoForItem(
        Transaction& transaction,
        std::string_view tableName,
        int64_t tableItemId);

    std::optional<SourcePhotoInfo> GetSourcePhotoForItem(
        Transaction& transaction,
        std::string_view tableName,
        int64_t tableItemId);

    std::optional<PhotoData> GetSourcePhotoData(
        Transaction& transaction,
        std::string_view tableName,
        int64_t tableItemId);

    ScaledPhotoResult GetOrCreateScaledPhoto(
        Transaction& transaction,
        int64_t sourcePhotoId,
        int width,
        int height);

    ScaledPhotoResult GetScaledPhotoForItem(
        Transaction& transaction,
        std::string_view tableName,
        int64_t tableItemId,
        int width,
        int height);

    bool HasPhoto(
        Transaction& transaction,
        std::string_view tableName,
        int64_t tableItemId);

    int64_t DeleteScaledPhotosOlderThan(
        Transaction& transaction,
        int64_t maxAgeUs);

    StorageStats GetScaledPhotoStorageStats(
        Transaction& transaction);

    int64_t DeleteOldestScaledPhotosUntilSize(
        Transaction& transaction,
        int64_t targetBytes);

private:
    DatabaseHelper databaseHelper_;
    Secrets::SecretsHelperPtr secretsHelper_;
    TableHelpers::PhotoInstances photoInstances_;
    TableHelpers::SourcePhotos sourcePhotos_;
    TableHelpers::ScaledPhotos scaledPhotos_;
    TableHelpers::PhotoSupportTables photoSupportTables_;
    TableHelpers::TableItemPhotos tableItemPhotos_;

    void CascadeDeleteSourcePhoto(
        Transaction& transaction, int64_t sourcePhotoId);
    PhotoData EnforceMaxDimensions(
        const std::vector<char>& imageBytes,
        std::string_view imageType,
        int maxWidth,
        int maxHeight);
    static int ImageTypeFromString(std::string_view type);
};

}  // namespace Images
