#include "bounding_rect.h"

namespace BoundingRect {

    namespace {

        Rect ScaleRectToWidth(const Rect& rect, int width) {
            Rect result;
            double ratio = static_cast<double>(width)
                / static_cast<double>(rect.width);
            result.width = width;
            result.height =
                static_cast<int>(static_cast<double>(rect.height) * ratio);
            return result;
        }

        Rect ScaleRectToHeight(const Rect& rect, int height) {
            Rect result;
            double ratio = static_cast<double>(height)
                / static_cast<double>(rect.height);
            result.height = height;
            result.width =
                static_cast<int>(static_cast<double>(rect.width) * ratio);
            return result;
        }

    }

    bool operator==(const Rect& rect1, const Rect& rect2) {
        return rect1.width == rect2.width && rect1.height == rect2.height;
    }

    Rect GetClippedRect(const Rect& inputRect, const Rect& clippedRect) {
        Rect result;
        // Four cases:
        //   * Input rect already is smaller than clipped rect
        //   * If eighter width or height is smaller than clipped rect,
        //     then scale the other dimension.
        //   * Scale width to fit in clipped and if height fits, use that.
        //   * Scale height to fit
        if (inputRect.height <= clippedRect.height
            && inputRect.width <= clippedRect.width) {
            return inputRect;
        }
        if (inputRect.height <= clippedRect.height) {
            return ScaleRectToWidth(inputRect, clippedRect.width);
        }
        if (inputRect.width <= clippedRect.width) {
            return ScaleRectToHeight(inputRect, clippedRect.height);
        }
        result = ScaleRectToWidth(inputRect, clippedRect.width);
        if (result.height <= clippedRect.height) {
            return result;
        }
        return ScaleRectToHeight(inputRect, clippedRect.height);
    }

}  // namespace BoundingRect
