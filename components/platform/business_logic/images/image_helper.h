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

// The HTTP Content-Type for a STORED image type ("png", "jpeg", "svg", ...).
//
// Not `"image/" + type`, which is what the serving endpoints used to build:
// that yields `image/svg` for a vector, and browsers will not render an SVG
// served under it — the registered type is `image/svg+xml`. Every other format
// this system stores happens to have a subtype equal to its stored name, which
// is why the concatenation went unnoticed until a vector arrived.
std::string ImageMimeType(std::string_view storedType);

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

    // The source image's size, for management screens that list what an item's
    // photo IS without wanting the bytes. `found` is false when the item has no
    // photo. Phase 6B: the Page Content editor shows this beside each row.
    struct PhotoDimensions {
        bool found = false;
        int width = 0;
        int height = 0;
        std::string type;
    };
    PhotoDimensions GetPhotoDimensions(
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

    // Is this a VECTOR image? (Polish Phase 11.)
    //
    // A vector is the one stored type that must never reach ImageResize:
    // there is nothing to resize — it scales inherently — and handing an SVG
    // to a decoder written for raster formats is exactly the shape of the
    // PNG-alpha bug (Phase 5), a format reaching a code path written for a
    // different one. ImageTypeFromString deliberately returns -1 for it, so
    // every caller that branches on that value has to ask this first.
    //
    // Accepts the stored subtype ("svg"), the MIME subtype ("svg+xml") and the
    // full MIME type, because uploads arrive spelled all three ways.
    static bool IsVectorType(std::string_view type);

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
