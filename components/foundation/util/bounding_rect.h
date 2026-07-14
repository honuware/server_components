#pragma once

namespace BoundingRect {

    struct Rect {
        int width = 0;
        int height = 0;
    };

    bool operator==(const Rect& rect1, const Rect& rect2);

    Rect GetClippedRect(const Rect& inputRect, const Rect& clippedRect);

}  // namespace BoundingRect
