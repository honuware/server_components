#include "util/file_util.h"

#include <gtest/gtest.h>

TEST(FileUtilTest, ReadFileBasic)
{
    std::string data = ReadFile("../components/testing/test/src/util/file_util_test_data.txt");
    ASSERT_EQ(data, "The test data.");
}