#pragma once

#include <cstddef>
#include <vector>

namespace ImageResize {

    enum ImageType { IMAGE_TYPE_BMP, IMAGE_TYPE_JPEG, IMAGE_TYPE_PNG, IMAGE_TYPE_TIFF};

    struct ImageDimensions {
        int width = 0;
        int height = 0;
    };

    ImageDimensions GetImageDimensions(const std::vector<char>& srcImage, ImageType imageType);
    std::vector<char> ResizeImage(const std::vector<char>& srcImage, int width, int height, ImageType imageType);

}  // namespace ImageResize
