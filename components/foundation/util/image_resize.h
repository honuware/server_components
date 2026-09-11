#pragma once

#include <cstddef>
#include <vector>

namespace ImageResize {

    // Phase 11: IMAGE_TYPE_TIFF removed. TIFF was never used, and libtiff was one
    // of the two original VS2026 blockers; dropping it takes libtiff out of all
    // three dependency graphs. Note libjpeg used to be reached transitively
    // THROUGH libtiff — honuware_foundation now links ${JPEG_LIB} explicitly
    // (see util/CMakeLists.txt), which had to land first.
    enum ImageType { IMAGE_TYPE_BMP, IMAGE_TYPE_JPEG, IMAGE_TYPE_PNG };

    struct ImageDimensions {
        int width = 0;
        int height = 0;
    };

    ImageDimensions GetImageDimensions(const std::vector<char>& srcImage, ImageType imageType);
    std::vector<char> ResizeImage(const std::vector<char>& srcImage, int width, int height, ImageType imageType);

}  // namespace ImageResize
