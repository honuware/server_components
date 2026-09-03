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

// ---- Polish Phase 11: vector (SVG) support ----

// A minimal but REAL svg document — the same shape an export produces, with an
// XML declaration ahead of the root element.
std::string_view MakeTestSvg() {
    return R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24">
  <path d="M4 4h16v16H4z"/>
</svg>
)SVG";
}

TEST(ImageHelperTest, IsVectorTypeAcceptsEverySpellingAndNothingElse) {
    // Uploads arrive spelled all three ways, and every raster type must stay
    // out — a false positive here would route a JPEG down the no-resize path.
    EXPECT_TRUE(ImageHelper::IsVectorType("svg"));
    EXPECT_TRUE(ImageHelper::IsVectorType("svg+xml"));
    EXPECT_TRUE(ImageHelper::IsVectorType("image/svg+xml"));
    for (const char* raster : {"jpeg", "jpg", "png", "gif", "webp", "bmp",
                               "tiff", "image/png", ""}) {
        EXPECT_FALSE(ImageHelper::IsVectorType(raster)) << raster;
    }
}

TEST(ImageHelperTest, ImageMimeTypeUsesTheRegisteredSvgType) {
    // "image/" + type would yield "image/svg", under which browsers refuse to
    // render the file. Every raster type happens to agree with concatenation,
    // which is why this went unnoticed until a vector arrived.
    EXPECT_EQ(ImageMimeType("svg"), "image/svg+xml");
    EXPECT_EQ(ImageMimeType("png"), "image/png");
    EXPECT_EQ(ImageMimeType("jpeg"), "image/jpeg");
}

TEST(ImageHelperTest, UploadStoresAnSvgByteForByte) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadSvg", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        TableHelpers::PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        auto secretsHelper = Secrets::Test::MakeTestSecretsHelper();
        ImageHelper imageHelper(databaseHelper, secretsHelper);

        const std::string_view svg = MakeTestSvg();
        const std::vector<char> bytes(svg.begin(), svg.end());
        UploadResult result = imageHelper.UploadAndAssociatePhoto(
            transaction, "people", 1, bytes, "svg");

        ASSERT_TRUE(result.success) << result.errorMessage;
        EXPECT_EQ(result.sourcePhoto.type, "svg");
        // 0x0 is the honest answer: no intrinsic RASTER size. A number here
        // would mean the server had parsed the file, which is the thing the
        // security position avoids.
        EXPECT_EQ(result.sourcePhoto.width, 0);
        EXPECT_EQ(result.sourcePhoto.height, 0);

        // Stored byte-for-byte — nothing re-encoded it on the way in.
        auto stored = imageHelper.GetSourcePhotoData(transaction, "people", 1);
        ASSERT_TRUE(stored.has_value());
        EXPECT_EQ(std::string(stored->bytes.begin(), stored->bytes.end()),
                  std::string(svg));
    });
}

TEST(ImageHelperTest, ScalingAnSvgReturnsTheSourceAndCachesNothing) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ScaleSvg", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        TableHelpers::PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        auto secretsHelper = Secrets::Test::MakeTestSecretsHelper();
        ImageHelper imageHelper(databaseHelper, secretsHelper);

        const std::string_view svg = MakeTestSvg();
        const std::vector<char> bytes(svg.begin(), svg.end());
        ASSERT_TRUE(imageHelper.UploadAndAssociatePhoto(
            transaction, "people", 1, bytes, "svg").success);

        // Ask for two DIFFERENT boxes. Both return the same source bytes: the
        // requested size is ignored because a vector scales inherently.
        for (const auto& box : std::vector<std::pair<int, int>>{{64, 64},
                                                               {944, 598}}) {
            ScaledPhotoResult scaled = imageHelper.GetScaledPhotoForItem(
                transaction, "people", 1, box.first, box.second);
            ASSERT_TRUE(scaled.success) << scaled.errorMessage;
            EXPECT_EQ(scaled.photo.type, "svg");
            EXPECT_EQ(std::string(scaled.photo.bytes.begin(),
                                  scaled.photo.bytes.end()),
                      std::string(svg))
                << "box " << box.first << "x" << box.second;
        }

        // And NOTHING was cached: there is no derivative to cache, and one row
        // per requested box would fill the table with identical copies of the
        // same file.
        StorageStats stats = imageHelper.GetScaledPhotoStorageStats(transaction);
        EXPECT_EQ(stats.totalScaledPhotos, 0);
    });
}

}  // namespace
}  // namespace Images
