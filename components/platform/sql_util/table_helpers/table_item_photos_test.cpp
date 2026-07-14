#include "table_item_photos.h"

#include <gtest/gtest.h>

#include "photo_instances.h"
#include "source_photos.h"
#include "photo_support_tables.h"
#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"

namespace TableHelpers {
namespace {

// Helper to create a source photo with backing photo instance
int64_t CreateTestSourcePhotoForItem(
    Transaction& transaction, DatabaseHelper databaseHelper) {
    PhotoInstances photoInstances(databaseHelper);
    int64_t piId = photoInstances.AddPhotoInstance(
        transaction, "item_photo_bytes", "jpeg", 800, 600);
    SourcePhotos sourcePhotos(databaseHelper);
    return sourcePhotos.AddSourcePhoto(transaction, piId);
}

TEST(TableItemPhotosTest, AddAndGetTableItemPhoto) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddAndGetTableItemPhoto", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        int64_t sourcePhotoId = CreateTestSourcePhotoForItem(transaction, databaseHelper);

        TableItemPhotos tableItemPhotos(databaseHelper);
        int64_t tipId = tableItemPhotos.AddTableItemPhoto(
            transaction, "people", 42, sourcePhotoId);
        ASSERT_GT(tipId, 0);

        KeyValueTable row = tableItemPhotos.GetTableItemPhoto(transaction, "people", 42);
        EXPECT_EQ(row.at("table_name"), "people");
        EXPECT_EQ(row.at("table_item_id"), "42");
        EXPECT_EQ(row.at("source_photo_id"), std::to_string(sourcePhotoId));
    });
}

TEST(TableItemPhotosTest, GetTableItemPhotoById) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetTableItemPhotoById", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        int64_t sourcePhotoId = CreateTestSourcePhotoForItem(transaction, databaseHelper);

        TableItemPhotos tableItemPhotos(databaseHelper);
        int64_t tipId = tableItemPhotos.AddTableItemPhoto(
            transaction, "people", 99, sourcePhotoId);

        KeyValueTable row = tableItemPhotos.GetTableItemPhotoById(transaction, tipId);
        EXPECT_EQ(row.at("table_name"), "people");
        EXPECT_EQ(row.at("table_item_id"), "99");
    });
}

TEST(TableItemPhotosTest, UpdateTableItemPhoto) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateTableItemPhoto", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        int64_t sourcePhotoId1 = CreateTestSourcePhotoForItem(transaction, databaseHelper);
        int64_t sourcePhotoId2 = CreateTestSourcePhotoForItem(transaction, databaseHelper);

        TableItemPhotos tableItemPhotos(databaseHelper);
        tableItemPhotos.AddTableItemPhoto(
            transaction, "people", 10, sourcePhotoId1);

        tableItemPhotos.UpdateTableItemPhoto(
            transaction, "people", 10, sourcePhotoId2);

        KeyValueTable row = tableItemPhotos.GetTableItemPhoto(transaction, "people", 10);
        EXPECT_EQ(row.at("source_photo_id"), std::to_string(sourcePhotoId2));
    });
}

TEST(TableItemPhotosTest, DeleteTableItemPhoto) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteTableItemPhoto", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        int64_t sourcePhotoId = CreateTestSourcePhotoForItem(transaction, databaseHelper);

        TableItemPhotos tableItemPhotos(databaseHelper);
        tableItemPhotos.AddTableItemPhoto(
            transaction, "people", 55, sourcePhotoId);

        tableItemPhotos.DeleteTableItemPhoto(transaction, "people", 55);

        KeyValueTable row = tableItemPhotos.GetTableItemPhoto(transaction, "people", 55);
        EXPECT_TRUE(row.empty());
    });
}

TEST(TableItemPhotosTest, HasPhoto) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HasPhoto", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        int64_t sourcePhotoId = CreateTestSourcePhotoForItem(transaction, databaseHelper);

        TableItemPhotos tableItemPhotos(databaseHelper);
        EXPECT_FALSE(tableItemPhotos.HasPhoto(transaction, "people", 77));

        tableItemPhotos.AddTableItemPhoto(
            transaction, "people", 77, sourcePhotoId);
        EXPECT_TRUE(tableItemPhotos.HasPhoto(transaction, "people", 77));
    });
}

TEST(TableItemPhotosTest, UniqueConstraintEnforced) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UniqueConstraint", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        int64_t sourcePhotoId1 = CreateTestSourcePhotoForItem(transaction, databaseHelper);
        int64_t sourcePhotoId2 = CreateTestSourcePhotoForItem(transaction, databaseHelper);

        TableItemPhotos tableItemPhotos(databaseHelper);
        tableItemPhotos.AddTableItemPhoto(
            transaction, "people", 33, sourcePhotoId1);

        try {
            tableItemPhotos.AddTableItemPhoto(
                transaction, "people", 33, sourcePhotoId2);
            FAIL() << "Expected exception on duplicate table_name/table_item_id";
        } catch (const std::exception& e) {
            std::string msg = e.what();
            EXPECT_TRUE(msg.find("duplicate") != std::string::npos ||
                        msg.find("unique") != std::string::npos ||
                        msg.find("constraint") != std::string::npos)
                << "Unexpected error: " << msg;
        }
    });
}

}  // namespace
}  // namespace TableHelpers
