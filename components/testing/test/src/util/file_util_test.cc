#include "util/file_util.h"

#include <filesystem>

#include <gtest/gtest.h>

TEST(FileUtilTest, ReadFileBasic)
{
    // Phase 4.2: derive the fixture path from __FILE__ (absolute at compile time)
    // so the test is independent of the process working directory AND of which
    // source tree it was compiled from — honuware standalone vs a consumer's
    // FetchContent clone. The previous "../components/..." path resolved relative
    // to the runtime CWD, which only worked while a components/ tree sat one level
    // above it. The fixture lives next to this file.
    const std::string dataPath =
        std::filesystem::path(__FILE__).parent_path().generic_string()
        + "/file_util_test_data.txt";
    std::string data = ReadFile(dataPath);
    ASSERT_EQ(data, "The test data.");
}