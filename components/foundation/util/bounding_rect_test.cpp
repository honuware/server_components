#include "bounding_rect.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace BoundingRect {
    namespace
    {

        using ::testing::Contains;
        using ::testing::ElementsAreArray;
        using ::testing::UnorderedElementsAre;
        using ::testing::ElementsAre;
        using ::testing::Eq;

        TEST(BoundingRectTest, BoundingRectAlreadyFits)
        {
            Rect rect{ 1, 1 };
            Rect rectClipped{ 2, 2 };
            ASSERT_EQ(GetClippedRect(rect, rectClipped), rect);
        }

        TEST(BoundingRectTest, BoundingRectWidthFits)
        {
            Rect rect{ 4, 10 };
            Rect rectClipped{ 6, 5 };
            Rect rectComp{ 2, 5 };
            ASSERT_EQ(GetClippedRect(rect, rectClipped), rectComp);
        }

        TEST(BoundingRectTest, BoundingRectHeightFits)
        {
            Rect rect{ 10, 4 };
            Rect rectClipped{ 5, 6 };
            Rect rectComp{ 5, 2 };
            ASSERT_EQ(GetClippedRect(rect, rectClipped), rectComp);
        }

        TEST(BoundingRectTest, BoundingRectNeitherFitsWidthBigger)
        {
            Rect rect{ 20, 12 };
            Rect rectClipped{ 10, 8 };
            Rect rectComp{ 10, 6 };
            ASSERT_EQ(GetClippedRect(rect, rectClipped), rectComp);
        }

        TEST(BoundingRectTest, BoundingRectNeitherFitsHeightBigger)
        {
            Rect rect{ 12, 20 };
            Rect rectClipped{ 8, 10 };
            Rect rectComp{ 6, 10 };
            ASSERT_EQ(GetClippedRect(rect, rectClipped), rectComp);
        }


    } // namespace {
}  // namespace BoundingRect
