#include "json_value_matcher.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace
{

    using ::testing::Contains;
    using ::testing::ElementsAreArray;
    using ::testing::UnorderedElementsAre;
    using ::testing::ElementsAre;
    using ::testing::Eq;

    TEST(JsonValueMatcherTest, JsonValueIsWvaluePositive)
    {
        Json::Value value1(5);
        Json::Value value2(5);
        EXPECT_THAT(value1, JsonValueIs(value2));
    }

    TEST(JsonValueMatcherTest, JsonValueIsTextPositive)
    {
        Json::Value value1(5);
        EXPECT_THAT(value1, JsonValueIs("5"));
    }

} // namespace {
