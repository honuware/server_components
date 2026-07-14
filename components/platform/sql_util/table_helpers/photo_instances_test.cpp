#include "photo_instances.h"

#include <gtest/gtest.h>

#include <iomanip>
#include <sstream>

#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"

namespace TableHelpers {
namespace {

std::string HexEncode(std::string_view input) {
    std::ostringstream oss;
    oss << "\\x";
    for (unsigned char c : input) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(c);
    }
    return oss.str();
}

TEST(PhotoInstancesTest, AddAndGetPhotoInstance) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddAndGetPhotoInstance", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoInstances photoInstances(databaseHelper);
        int64_t id = photoInstances.AddPhotoInstance(
            transaction, "fake_photo_bytes", "jpeg", 800, 600);
        ASSERT_GT(id, 0);

        KeyValueTable row = photoInstances.GetPhotoInstance(transaction, id);
        EXPECT_EQ(row.at("photo"), HexEncode("fake_photo_bytes"));
        EXPECT_EQ(row.at("type"), "jpeg");
        EXPECT_EQ(row.at("width"), "800");
        EXPECT_EQ(row.at("height"), "600");

        int64_t createdAtUs = std::stoll(row.at("created_at_us"));
        EXPECT_GT(createdAtUs, 0);
    });
}

TEST(PhotoInstancesTest, UpdatePhotoInstance) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdatePhotoInstance", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoInstances photoInstances(databaseHelper);
        int64_t id = photoInstances.AddPhotoInstance(
            transaction, "original_bytes", "png", 100, 200);

        photoInstances.UpdatePhotoInstance(
            transaction, id, "new_bytes", "jpeg", 300, 400);

        KeyValueTable row = photoInstances.GetPhotoInstance(transaction, id);
        EXPECT_EQ(row.at("photo"), HexEncode("new_bytes"));
        EXPECT_EQ(row.at("type"), "jpeg");
        EXPECT_EQ(row.at("width"), "300");
        EXPECT_EQ(row.at("height"), "400");
    });
}

TEST(PhotoInstancesTest, DeletePhotoInstance) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeletePhotoInstance", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoInstances photoInstances(databaseHelper);
        int64_t id = photoInstances.AddPhotoInstance(
            transaction, "bytes_to_delete", "jpeg", 50, 50);
        ASSERT_GT(id, 0);

        photoInstances.DeletePhotoInstance(transaction, id);

        KeyValueTable row = photoInstances.GetPhotoInstance(transaction, id);
        EXPECT_TRUE(row.empty());
    });
}

}  // namespace
}  // namespace TableHelpers
