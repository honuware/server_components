#include "image_helper.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "db_schema/photos.h"
#include "util/secrets/secret_keys.h"
#include "util/bounding_rect.h"
#include "util/image_resize.h"
#include "util/types.h"

namespace Images {

namespace {

// Encode binary data to PostgreSQL bytea hex format (\xABCD...)
std::string ByteaHexEncode(const std::vector<char>& bytes) {
    std::ostringstream oss;
    oss << "\\x";
    for (unsigned char c : bytes) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(c);
    }
    return oss.str();
}

// Decode PostgreSQL bytea hex format (\xABCD...) to binary data
std::vector<char> ByteaHexDecode(const std::string& hex) {
    if (hex.size() < 2 || hex[0] != '\\' || hex[1] != 'x') {
        return std::vector<char>(hex.begin(), hex.end());
    }
    std::vector<char> result;
    result.reserve((hex.size() - 2) / 2);
    for (size_t i = 2; i + 1 < hex.size(); i += 2) {
        unsigned char byte = static_cast<unsigned char>(
            std::stoi(hex.substr(i, 2), nullptr, 16));
        result.push_back(static_cast<char>(byte));
    }
    return result;
}

}  // namespace

ImageHelper::ImageHelper(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper)
    , secretsHelper_(nullptr)
    , photoInstances_(databaseHelper)
    , sourcePhotos_(databaseHelper)
    , scaledPhotos_(databaseHelper)
    , photoSupportTables_(databaseHelper)
    , tableItemPhotos_(databaseHelper) {
}

ImageHelper::ImageHelper(
    DatabaseHelper databaseHelper,
    Secrets::SecretsHelperPtr secretsHelper)
    : databaseHelper_(databaseHelper)
    , secretsHelper_(secretsHelper)
    , photoInstances_(databaseHelper)
    , sourcePhotos_(databaseHelper)
    , scaledPhotos_(databaseHelper)
    , photoSupportTables_(databaseHelper)
    , tableItemPhotos_(databaseHelper) {
}

int ImageHelper::ImageTypeFromString(std::string_view type) {
    if (type == "jpeg" || type == "jpg") {
        return ImageResize::IMAGE_TYPE_JPEG;
    } else if (type == "png") {
        return ImageResize::IMAGE_TYPE_PNG;
    } else if (type == "bmp") {
        return ImageResize::IMAGE_TYPE_BMP;
    } else if (type == "tiff") {
        return ImageResize::IMAGE_TYPE_TIFF;
    }
    return -1;
}

PhotoData ImageHelper::EnforceMaxDimensions(
    const std::vector<char>& imageBytes,
    std::string_view imageType,
    int maxWidth,
    int maxHeight) {
    int imageTypeEnum = ImageTypeFromString(imageType);
    auto dims = ImageResize::GetImageDimensions(
        imageBytes, static_cast<ImageResize::ImageType>(imageTypeEnum));

    PhotoData result;
    result.type = std::string(imageType);

    if (dims.width <= maxWidth && dims.height <= maxHeight) {
        result.bytes = imageBytes;
        result.width = dims.width;
        result.height = dims.height;
        return result;
    }

    BoundingRect::Rect inputRect{dims.width, dims.height};
    BoundingRect::Rect boundingRect{maxWidth, maxHeight};
    BoundingRect::Rect clipped = BoundingRect::GetClippedRect(
        inputRect, boundingRect);

    result.bytes = ImageResize::ResizeImage(
        imageBytes, clipped.width, clipped.height,
        static_cast<ImageResize::ImageType>(imageTypeEnum));
    result.width = clipped.width;
    result.height = clipped.height;
    return result;
}

void ImageHelper::CascadeDeleteSourcePhoto(
    Transaction& transaction, int64_t sourcePhotoId) {
    // DeleteScaledPhotosBySourcePhotoId deletes scaled photos and their instances
    scaledPhotos_.DeleteScaledPhotosBySourcePhotoId(transaction, sourcePhotoId);
    // DeleteSourcePhoto deletes the source photo and its photo instance
    sourcePhotos_.DeleteSourcePhoto(transaction, sourcePhotoId);
}

UploadResult ImageHelper::UploadAndAssociatePhoto(
    Transaction& transaction,
    std::string_view tableName,
    int64_t tableItemId,
    const std::vector<char>& imageBytes,
    std::string_view imageType) {
    UploadResult result;

    // Validate table is supported for photos
    if (!photoSupportTables_.IsPhotoSupportedForTable(transaction, tableName)) {
        result.errorMessage = "Photos are not supported for table '"
            + std::string(tableName) + "'";
        return result;
    }

    // Validate image type
    int imageTypeEnum = ImageTypeFromString(imageType);
    if (imageTypeEnum < 0) {
        result.errorMessage = "Unsupported image type '"
            + std::string(imageType) + "'";
        return result;
    }

    // Enforce max dimensions
    int maxWidth = 4096;
    int maxHeight = 4096;
    if (secretsHelper_) {
        std::string maxWidthStr = secretsHelper_->LookupSecret(
            transaction, Secrets::kImageMaxSourceWidth);
        std::string maxHeightStr = secretsHelper_->LookupSecret(
            transaction, Secrets::kImageMaxSourceHeight);
        if (!maxWidthStr.empty()) {
            maxWidth = std::stoi(maxWidthStr);
        }
        if (!maxHeightStr.empty()) {
            maxHeight = std::stoi(maxHeightStr);
        }
    }

    PhotoData photo = EnforceMaxDimensions(
        imageBytes, imageType, maxWidth, maxHeight);

    // If item already has a photo, delete the old one
    if (tableItemPhotos_.HasPhoto(transaction, tableName, tableItemId)) {
        DeletePhotoForItem(transaction, tableName, tableItemId);
    }

    // Create photo instance with the image bytes (hex-encoded for bytea)
    std::string photoString = ByteaHexEncode(photo.bytes);
    int64_t photoInstanceId = photoInstances_.AddPhotoInstance(
        transaction, photoString, imageType, photo.width, photo.height);

    // Create source photo
    int64_t sourcePhotoId = sourcePhotos_.AddSourcePhoto(
        transaction, photoInstanceId);

    // Associate with table item
    tableItemPhotos_.AddTableItemPhoto(
        transaction, tableName, tableItemId, sourcePhotoId);

    // Build result
    result.success = true;
    KeyValueTable sourceRow = sourcePhotos_.GetSourcePhotoWithInstance(
        transaction, sourcePhotoId);
    result.sourcePhoto.id = sourcePhotoId;
    result.sourcePhoto.photoInstanceId = photoInstanceId;
    result.sourcePhoto.type = std::string(imageType);
    result.sourcePhoto.width = photo.width;
    result.sourcePhoto.height = photo.height;
    if (sourceRow.count("created_at_us")) {
        result.sourcePhoto.createdAtUs = std::stoll(
            sourceRow.at("created_at_us"));
    }
    if (sourceRow.count("last_updated_at_us")) {
        result.sourcePhoto.lastUpdatedAtUs = std::stoll(
            sourceRow.at("last_updated_at_us"));
    }

    return result;
}

bool ImageHelper::DeletePhotoForItem(
    Transaction& transaction,
    std::string_view tableName,
    int64_t tableItemId) {
    KeyValueTable tipRow = tableItemPhotos_.GetTableItemPhoto(
        transaction, tableName, tableItemId);
    if (tipRow.empty()) {
        return false;
    }

    int64_t sourcePhotoId = std::stoll(tipRow.at("source_photo_id"));
    tableItemPhotos_.DeleteTableItemPhoto(transaction, tableName, tableItemId);
    CascadeDeleteSourcePhoto(transaction, sourcePhotoId);
    return true;
}

std::optional<SourcePhotoInfo> ImageHelper::GetSourcePhotoForItem(
    Transaction& transaction,
    std::string_view tableName,
    int64_t tableItemId) {
    KeyValueTable tipRow = tableItemPhotos_.GetTableItemPhoto(
        transaction, tableName, tableItemId);
    if (tipRow.empty()) {
        return std::nullopt;
    }

    int64_t sourcePhotoId = std::stoll(tipRow.at("source_photo_id"));
    KeyValueTable sourceRow = sourcePhotos_.GetSourcePhotoWithInstance(
        transaction, sourcePhotoId);
    if (sourceRow.empty()) {
        return std::nullopt;
    }

    SourcePhotoInfo info;
    info.id = sourcePhotoId;
    info.photoInstanceId = std::stoll(
        sourceRow.at(std::string(DbSchema::kSourcePhotoPhotoInstanceId)));
    info.type = sourceRow.at("type");
    info.width = std::stoi(sourceRow.at("width"));
    info.height = std::stoi(sourceRow.at("height"));
    if (sourceRow.count("created_at_us")) {
        info.createdAtUs = std::stoll(sourceRow.at("created_at_us"));
    }
    if (sourceRow.count("last_updated_at_us")) {
        info.lastUpdatedAtUs = std::stoll(sourceRow.at("last_updated_at_us"));
    }
    return info;
}

std::optional<PhotoData> ImageHelper::GetSourcePhotoData(
    Transaction& transaction,
    std::string_view tableName,
    int64_t tableItemId) {
    auto sourceInfo = GetSourcePhotoForItem(transaction, tableName, tableItemId);
    if (!sourceInfo.has_value()) {
        return std::nullopt;
    }

    KeyValueTable instanceRow = photoInstances_.GetPhotoInstance(
        transaction, sourceInfo->photoInstanceId);
    if (instanceRow.empty()) {
        return std::nullopt;
    }

    PhotoData data;
    data.bytes = ByteaHexDecode(instanceRow.at("photo"));
    data.type = instanceRow.at("type");
    data.width = std::stoi(instanceRow.at("width"));
    data.height = std::stoi(instanceRow.at("height"));
    return data;
}

ScaledPhotoResult ImageHelper::GetOrCreateScaledPhoto(
    Transaction& transaction,
    int64_t sourcePhotoId,
    int width,
    int height) {
    ScaledPhotoResult result;

    // Check cache first
    KeyValueTable cachedRow = scaledPhotos_.GetScaledPhotoBySourceAndDimensions(
        transaction, sourcePhotoId, width, height);
    if (!cachedRow.empty()) {
        int64_t scaledPhotoId = std::stoll(
            cachedRow.at(std::string(DbSchema::kScaledPhotoId)));
        int64_t photoInstanceId = std::stoll(
            cachedRow.at(std::string(DbSchema::kScaledPhotoPhotoInstanceId)));

        // Update last used timestamp
        scaledPhotos_.UpdateLastUsedAtUs(transaction, scaledPhotoId);

        // Fetch the photo bytes
        KeyValueTable instanceRow = photoInstances_.GetPhotoInstance(
            transaction, photoInstanceId);
        if (!instanceRow.empty()) {
            result.success = true;
            result.photo.bytes = ByteaHexDecode(instanceRow.at("photo"));
            result.photo.type = instanceRow.at("type");
            result.photo.width = std::stoi(instanceRow.at("width"));
            result.photo.height = std::stoi(instanceRow.at("height"));
            return result;
        }
    }

    // Cache miss - fetch source photo and resize
    KeyValueTable sourceRow = sourcePhotos_.GetSourcePhotoWithInstance(
        transaction, sourcePhotoId);
    if (sourceRow.empty()) {
        result.errorMessage = "Source photo not found";
        return result;
    }

    int64_t sourceInstanceId = std::stoll(
        sourceRow.at(std::string(DbSchema::kSourcePhotoPhotoInstanceId)));
    KeyValueTable sourceInstanceRow = photoInstances_.GetPhotoInstance(
        transaction, sourceInstanceId);
    if (sourceInstanceRow.empty()) {
        result.errorMessage = "Source photo instance not found";
        return result;
    }

    std::string sourceType = sourceInstanceRow.at("type");
    int imageTypeEnum = ImageTypeFromString(sourceType);
    if (imageTypeEnum < 0) {
        result.errorMessage = "Unsupported source image type";
        return result;
    }

    // Decode bytea hex and resize the image, preserving aspect ratio
    std::vector<char> sourceBytes = ByteaHexDecode(sourceInstanceRow.at("photo"));
    int sourceWidth = std::stoi(sourceInstanceRow.at("width"));
    int sourceHeight = std::stoi(sourceInstanceRow.at("height"));

    BoundingRect::Rect sourceRect{sourceWidth, sourceHeight};
    BoundingRect::Rect boundingRect{width, height};
    BoundingRect::Rect fitted = BoundingRect::GetClippedRect(
        sourceRect, boundingRect);

    std::vector<char> scaledBytes = ImageResize::ResizeImage(
        sourceBytes, fitted.width, fitted.height,
        static_cast<ImageResize::ImageType>(imageTypeEnum));

    // Store the scaled photo instance (hex-encoded for bytea)
    std::string scaledPhotoStr = ByteaHexEncode(scaledBytes);
    int64_t scaledInstanceId = photoInstances_.AddPhotoInstance(
        transaction, scaledPhotoStr, sourceType, fitted.width, fitted.height);

    // Create scaled photo record
    scaledPhotos_.AddScaledPhoto(
        transaction, sourcePhotoId, scaledInstanceId, width, height);

    result.success = true;
    result.photo.bytes = scaledBytes;
    result.photo.type = sourceType;
    result.photo.width = fitted.width;
    result.photo.height = fitted.height;
    return result;
}

ScaledPhotoResult ImageHelper::GetScaledPhotoForItem(
    Transaction& transaction,
    std::string_view tableName,
    int64_t tableItemId,
    int width,
    int height) {
    auto sourceInfo = GetSourcePhotoForItem(transaction, tableName, tableItemId);
    if (!sourceInfo.has_value()) {
        ScaledPhotoResult result;
        result.errorMessage = "No photo found for item";
        return result;
    }
    auto result = GetOrCreateScaledPhoto(transaction, sourceInfo->id, width, height);
    result.sourceLastUpdatedAtUs = sourceInfo->lastUpdatedAtUs;
    return result;
}

bool ImageHelper::HasPhoto(
    Transaction& transaction,
    std::string_view tableName,
    int64_t tableItemId) {
    return tableItemPhotos_.HasPhoto(transaction, tableName, tableItemId);
}

int64_t ImageHelper::DeleteScaledPhotosOlderThan(
    Transaction& transaction,
    int64_t maxAgeUs) {
    // Get current time from the database
    std::string nowStr = transaction.RunSqlStatementReturningOneValue(
        "SELECT now_us()");
    int64_t nowUs = std::stoll(nowStr);
    int64_t cutoffUs = nowUs - maxAgeUs;

    KeyValueTableArray oldPhotos = scaledPhotos_.GetScaledPhotosOlderThan(
        transaction, cutoffUs);

    int64_t deletedCount = 0;
    for (const auto& row : oldPhotos) {
        int64_t scaledPhotoId = std::stoll(
            row.at(std::string(DbSchema::kScaledPhotoId)));
        // DeleteScaledPhoto also deletes the associated photo instance
        scaledPhotos_.DeleteScaledPhoto(transaction, scaledPhotoId);
        ++deletedCount;
    }
    return deletedCount;
}

StorageStats ImageHelper::GetScaledPhotoStorageStats(
    Transaction& transaction) {
    StorageStats stats;
    KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
        "SELECT COUNT(*) AS total_count, "
        "COALESCE(SUM(octet_length(pi.photo)), 0) AS total_bytes "
        "FROM scaled_photos sp "
        "JOIN photo_instances pi ON sp.photo_instance_id = pi.photo_instance_id");
    if (!row.empty()) {
        stats.totalScaledPhotos = std::stoll(row.at("total_count"));
        stats.totalStorageBytes = std::stoll(row.at("total_bytes"));
    }
    return stats;
}

int64_t ImageHelper::DeleteOldestScaledPhotosUntilSize(
    Transaction& transaction,
    int64_t targetBytes) {
    int64_t deletedCount = 0;

    while (true) {
        int64_t currentBytes = scaledPhotos_.GetTotalScaledPhotoStorageBytes(
            transaction);
        if (currentBytes <= targetBytes) {
            break;
        }

        KeyValueTableArray oldest = scaledPhotos_.GetScaledPhotosOrderedByLastUsed(
            transaction, 1);
        if (oldest.empty()) {
            break;
        }

        int64_t scaledPhotoId = std::stoll(
            oldest[0].at(std::string(DbSchema::kScaledPhotoId)));
        // DeleteScaledPhoto also deletes the associated photo instance
        scaledPhotos_.DeleteScaledPhoto(transaction, scaledPhotoId);
        ++deletedCount;
    }
    return deletedCount;
}

}  // namespace Images
