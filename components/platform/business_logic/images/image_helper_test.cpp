#include "image_helper.h"

#include <gtest/gtest.h>

#include <boost/gil.hpp>
#include <boost/gil/extension/io/jpeg.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>

#include "util/secrets/secrets_helper_test_util.h"
#include "test/src/util/database_test_helper.h"

namespace Images {
namespace {

using VectorSink = boost::iostreams::back_insert_device<std::vector<char>>;
using VectorStream = boost::iostreams::stream<VectorSink>;

std::vector<char> MakeTestJpeg(int width, int height) {
    boost::gil::rgb8_image_t img(width, height);
    auto view = boost::gil::view(img);
    boost::gil::rgb8_pixel_t blue(0, 0, 255);
    for (int y = 0; y < view.height(); ++y) {
        auto iter = view.row_begin(y);
        for (int x = 0; x < view.width(); ++x) {
            iter[x] = blue;
        }
    }
    std::vector<char> result;
    VectorSink sink(result);
    VectorStream stream(sink);
    boost::gil::write_view(stream, boost::gil::const_view(img),
                           boost::gil::jpeg_tag());
    return result;
}

TEST(ImageHelperTest, UploadAndAssociatePhotoSuccess) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadSuccess", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        // Register "people" as a photo-supported table
        TableHelpers::PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        auto secretsHelper = Secrets::Test::MakeTestSecretsHelper();
        ImageHelper imageHelper(databaseHelper, secretsHelper);

        std::vector<char> jpeg = MakeTestJpeg(64, 48);
        UploadResult result = imageHelper.UploadAndAssociatePhoto(
            transaction, "people", 1, jpeg, "jpeg");

        EXPECT_TRUE(result.success);
        EXPECT_TRUE(result.errorMessage.empty());
        EXPECT_GT(result.sourcePhoto.id, 0);
        EXPECT_GT(result.sourcePhoto.photoInstanceId, 0);
        EXPECT_EQ(result.sourcePhoto.type, "jpeg");
        EXPECT_EQ(result.sourcePhoto.width, 64);
        EXPECT_EQ(result.sourcePhoto.height, 48);
    });
}

TEST(ImageHelperTest, UploadPhotoUnsupportedTable) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UnsupportedTable", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        ImageHelper imageHelper(databaseHelper);

        std::vector<char> jpeg = MakeTestJpeg(32, 32);
        UploadResult result = imageHelper.UploadAndAssociatePhoto(
            transaction, "nonexistent_table", 1, jpeg, "jpeg");

        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.errorMessage.empty());
    });
}

TEST(ImageHelperTest, UploadPhotoUnsupportedType) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UnsupportedType", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        TableHelpers::PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        ImageHelper imageHelper(databaseHelper);

        std::vector<char> jpeg = MakeTestJpeg(32, 32);
        UploadResult result = imageHelper.UploadAndAssociatePhoto(
            transaction, "people", 1, jpeg, "webp");

        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.errorMessage.empty());
    });
}

TEST(ImageHelperTest, UploadPhotoReplacesExisting) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ReplaceExisting", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        TableHelpers::PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        auto secretsHelper = Secrets::Test::MakeTestSecretsHelper();
        ImageHelper imageHelper(databaseHelper, secretsHelper);

        std::vector<char> jpeg1 = MakeTestJpeg(64, 48);
        UploadResult result1 = imageHelper.UploadAndAssociatePhoto(
            transaction, "people", 1, jpeg1, "jpeg");
        ASSERT_TRUE(result1.success);
        int64_t firstSourcePhotoId = result1.sourcePhoto.id;

        std::vector<char> jpeg2 = MakeTestJpeg(80, 60);
        UploadResult result2 = imageHelper.UploadAndAssociatePhoto(
            transaction, "people", 1, jpeg2, "jpeg");
        ASSERT_TRUE(result2.success);

        // New photo should have different source photo ID
        EXPECT_NE(result2.sourcePhoto.id, firstSourcePhotoId);
        EXPECT_EQ(result2.sourcePhoto.width, 80);
        EXPECT_EQ(result2.sourcePhoto.height, 60);
    });
}

TEST(ImageHelperTest, DeletePhotoForItemSuccess) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteSuccess", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        TableHelpers::PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        auto secretsHelper = Secrets::Test::MakeTestSecretsHelper();
        ImageHelper imageHelper(databaseHelper, secretsHelper);

        std::vector<char> jpeg = MakeTestJpeg(64, 48);
        imageHelper.UploadAndAssociatePhoto(
            transaction, "people", 1, jpeg, "jpeg");

        bool deleted = imageHelper.DeletePhotoForItem(
            transaction, "people", 1);
        EXPECT_TRUE(deleted);

        EXPECT_FALSE(imageHelper.HasPhoto(transaction, "people", 1));
    });
}

TEST(ImageHelperTest, DeletePhotoForItemNoPhoto) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteNoPhoto", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        ImageHelper imageHelper(databaseHelper);

        bool deleted = imageHelper.DeletePhotoForItem(
            transaction, "people", 999);
        EXPECT_FALSE(deleted);
    });
}

TEST(ImageHelperTest, GetSourcePhotoForItemFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetSourceFound", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        TableHelpers::PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        auto secretsHelper = Secrets::Test::MakeTestSecretsHelper();
        ImageHelper imageHelper(databaseHelper, secretsHelper);

        std::vector<char> jpeg = MakeTestJpeg(64, 48);
        imageHelper.UploadAndAssociatePhoto(
            transaction, "people", 1, jpeg, "jpeg");

        auto info = imageHelper.GetSourcePhotoForItem(
            transaction, "people", 1);
        ASSERT_TRUE(info.has_value());
        EXPECT_GT(info->id, 0);
        EXPECT_EQ(info->type, "jpeg");
        EXPECT_EQ(info->width, 64);
        EXPECT_EQ(info->height, 48);
    });
}

TEST(ImageHelperTest, GetSourcePhotoForItemNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetSourceNotFound", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        ImageHelper imageHelper(databaseHelper);

        auto info = imageHelper.GetSourcePhotoForItem(
            transaction, "people", 999);
        EXPECT_FALSE(info.has_value());
    });
}

TEST(ImageHelperTest, HasPhoto) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HasPhoto", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        TableHelpers::PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        auto secretsHelper = Secrets::Test::MakeTestSecretsHelper();
        ImageHelper imageHelper(databaseHelper, secretsHelper);

        EXPECT_FALSE(imageHelper.HasPhoto(transaction, "people", 1));

        std::vector<char> jpeg = MakeTestJpeg(32, 32);
        imageHelper.UploadAndAssociatePhoto(
            transaction, "people", 1, jpeg, "jpeg");

        EXPECT_TRUE(imageHelper.HasPhoto(transaction, "people", 1));
    });
}

TEST(ImageHelperTest, GetScaledPhotoStorageStatsEmpty) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("StorageStatsEmpty", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        ImageHelper imageHelper(databaseHelper);

        StorageStats stats = imageHelper.GetScaledPhotoStorageStats(transaction);
        EXPECT_EQ(stats.totalScaledPhotos, 0);
        EXPECT_EQ(stats.totalStorageBytes, 0);
    });
}

}  // namespace
}  // namespace Images
