#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Images {

struct SourcePhotoInfo {
    int64_t id = 0;
    int64_t photoInstanceId = 0;
    std::string type;
    int width = 0;
    int height = 0;
    int64_t createdAtUs = 0;
    int64_t lastUpdatedAtUs = 0;
};

struct ScaledPhotoInfo {
    int64_t id = 0;
    int64_t sourcePhotoId = 0;
    int64_t photoInstanceId = 0;
    std::string type;
    int width = 0;
    int height = 0;
    int64_t createdAtUs = 0;
    int64_t lastUsedAtUs = 0;
};

struct PhotoData {
    std::vector<char> bytes;
    std::string type;
    int width = 0;
    int height = 0;
};

struct UploadResult {
    bool success = false;
    std::string errorMessage;
    SourcePhotoInfo sourcePhoto;
};

struct ScaledPhotoResult {
    bool success = false;
    std::string errorMessage;
    PhotoData photo;
    int64_t sourceLastUpdatedAtUs = 0;
};

struct StorageStats {
    int64_t totalScaledPhotos = 0;
    int64_t totalStorageBytes = 0;
};

}  // namespace Images
