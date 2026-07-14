#include "source_photos.h"

#include <gtest/gtest.h>

#include <iomanip>
#include <sstream>

#include "photo_instances.h"
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

TEST(SourcePhotosTest, AddAndGetSourcePhoto) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddAndGetSourcePhoto", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoInstances photoInstances(databaseHelper);
        int64_t piId = photoInstances.AddPhotoInstance(
            transaction, "photo_bytes", "jpeg", 800, 600);

        SourcePhotos sourcePhotos(databaseHelper);
        int64_t spId = sourcePhotos.AddSourcePhoto(transaction, piId);
        ASSERT_GT(spId, 0);

        KeyValueTable row = sourcePhotos.GetSourcePhoto(transaction, spId);
        EXPECT_EQ(row.at("photo_instance_id"), std::to_string(piId));

        int64_t createdAtUs = std::stoll(row.at("created_at_us"));
        EXPECT_GT(createdAtUs, 0);
    });
}

TEST(SourcePhotosTest, GetSourcePhotoWithInstance) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetSourcePhotoWithInstance", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoInstances photoInstances(databaseHelper);
        int64_t piId = photoInstances.AddPhotoInstance(
            transaction, "joined_bytes", "png", 1024, 768);

        SourcePhotos sourcePhotos(databaseHelper);
        int64_t spId = sourcePhotos.AddSourcePhoto(transaction, piId);

        KeyValueTable row = sourcePhotos.GetSourcePhotoWithInstance(transaction, spId);
        EXPECT_EQ(row.at("photo"), HexEncode("joined_bytes"));
        EXPECT_EQ(row.at("type"), "png");
        EXPECT_EQ(row.at("width"), "1024");
        EXPECT_EQ(row.at("height"), "768");
    });
}

TEST(SourcePhotosTest, DeleteSourcePhotoCascadesToPhotoInstance) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteSourcePhotoCascades", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoInstances photoInstances(databaseHelper);
        int64_t piId = photoInstances.AddPhotoInstance(
            transaction, "delete_me", "jpeg", 100, 100);

        SourcePhotos sourcePhotos(databaseHelper);
        int64_t spId = sourcePhotos.AddSourcePhoto(transaction, piId);

        sourcePhotos.DeleteSourcePhoto(transaction, spId);

        // Source photo should be gone
        KeyValueTable spRow = sourcePhotos.GetSourcePhoto(transaction, spId);
        EXPECT_TRUE(spRow.empty());

        // Photo instance should also be deleted
        KeyValueTable piRow = photoInstances.GetPhotoInstance(transaction, piId);
        EXPECT_TRUE(piRow.empty());
    });
}

TEST(SourcePhotosTest, UpdateSourcePhotoTimestamp) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateSourcePhotoTimestamp", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoInstances photoInstances(databaseHelper);
        int64_t piId = photoInstances.AddPhotoInstance(
            transaction, "timestamp_test", "jpeg", 200, 200);

        SourcePhotos sourcePhotos(databaseHelper);
        int64_t spId = sourcePhotos.AddSourcePhoto(transaction, piId);

        KeyValueTable before = sourcePhotos.GetSourcePhoto(transaction, spId);
        int64_t beforeTimestamp = std::stoll(before.at("last_updated_at_us"));

        sourcePhotos.UpdateSourcePhotoTimestamp(transaction, spId);

        KeyValueTable after = sourcePhotos.GetSourcePhoto(transaction, spId);
        int64_t afterTimestamp = std::stoll(after.at("last_updated_at_us"));
        EXPECT_GE(afterTimestamp, beforeTimestamp);
    });
}

}  // namespace
}  // namespace TableHelpers
