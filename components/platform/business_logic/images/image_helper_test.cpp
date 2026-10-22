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

// Phase 11: "tiff" used to map to IMAGE_TYPE_TIFF. It is now an unsupported type
// and an upload must fail CLEANLY — rejected at validation with a message naming
// the type, never reaching a decoder that no longer exists.
//
// ASSERTED rather than merely deleted. Removing the old expectation would have
// left nothing pinning the new behaviour, and a future "restore TIFF" could land
// silently; this is the test that has to be confronted first. Note that resizing
// a TIFF never actually worked (the output sink cannot seek, so it threw for
// every input), so nothing that previously succeeded stops working here.
//
// Goes through the PUBLIC upload path rather than the private
// ImageTypeFromString: what matters is the behaviour a caller sees, and testing
// it this way needs no change to production visibility.
TEST(ImageHelperTest, UploadPhotoRejectsTiffAsUnsupported) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadTiffRejected", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        TableHelpers::PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        auto secretsHelper = Secrets::Test::MakeTestSecretsHelper();
        ImageHelper imageHelper(databaseHelper, secretsHelper);

        // The bytes are a valid JPEG on purpose: the rejection must come from the
        // declared TYPE, before anything tries to decode. If validation were ever
        // reordered after the decode, this would stop failing for the right reason.
        std::vector<char> bytes = MakeTestJpeg(64, 48);

        UploadResult result = imageHelper.UploadAndAssociatePhoto(
            transaction, "people", 1, bytes, "tiff");

        EXPECT_FALSE(result.success);
        EXPECT_EQ(result.errorMessage, "Unsupported image type 'tiff'");
        EXPECT_EQ(result.sourcePhoto.id, 0)
            << "a rejected upload must not create a source photo row";
    });
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

// ---- Finishing website 14.1: an upload labelled "svg" must BE one ----

// What may legally come before the root element, in the shapes real exports
// produce. The Illustrator head is the studio's own safe-space badge.
TEST(ImageHelperTest, IsSvgDocumentAcceptsWhatMayPrecedeTheRoot) {
    EXPECT_TRUE(ImageHelper::IsSvgDocument(MakeTestSvg()));
    EXPECT_TRUE(ImageHelper::IsSvgDocument(
        "<svg width=\"351\" height=\"102\" viewBox=\"0 0 351 102\"></svg>"));
    EXPECT_TRUE(ImageHelper::IsSvgDocument("<svg/>"));
    EXPECT_TRUE(ImageHelper::IsSvgDocument("<svg>"));
    EXPECT_TRUE(ImageHelper::IsSvgDocument("\n\t <svg\nviewBox=\"0 0 1 1\"/>"));
    EXPECT_TRUE(ImageHelper::IsSvgDocument("\xEF\xBB\xBF<svg/>"))
        << "a UTF-8 byte-order mark";
    EXPECT_TRUE(ImageHelper::IsSvgDocument(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<!-- Generator: Adobe Illustrator 24.2.1, SVG Export Plug-In . "
        "SVG Version: 6.00 Build 0)  -->\r\n"
        "<svg version=\"1.1\" id=\"Layer_1\"></svg>"));
    EXPECT_TRUE(ImageHelper::IsSvgDocument(
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
        "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n<svg/>"));
    // Illustrator's "with entities" export: the subset's own '>' must not end
    // the DOCTYPE early.
    EXPECT_TRUE(ImageHelper::IsSvgDocument(
        "<!DOCTYPE svg [\n  <!ENTITY ns_svg \"http://www.w3.org/2000/svg\">\n"
        "  <!ENTITY ns_xlink \"http://www.w3.org/1999/xlink\">\n]>\n<svg/>"));
}

TEST(ImageHelperTest, IsSvgDocumentRefusesEverythingElse) {
    EXPECT_FALSE(ImageHelper::IsSvgDocument(""));
    EXPECT_FALSE(ImageHelper::IsSvgDocument("   \n"));
    // A PNG's signature, then the start of its header chunk.
    EXPECT_FALSE(ImageHelper::IsSvgDocument(
        std::string_view("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16)));
    // A JPEG's start-of-image marker.
    EXPECT_FALSE(ImageHelper::IsSvgDocument(std::string_view("\xff\xd8\xff\xe0", 4)));
    // An HTML page with an inline SVG is still an HTML page.
    EXPECT_FALSE(ImageHelper::IsSvgDocument(
        "<!DOCTYPE html><html><body><svg/></body></html>"));
    EXPECT_FALSE(ImageHelper::IsSvgDocument("<html><svg/></html>"));
    EXPECT_FALSE(ImageHelper::IsSvgDocument("hello <svg/>"));
    // The element name must be exactly "svg".
    EXPECT_FALSE(ImageHelper::IsSvgDocument("<svgfoo/>"));
    EXPECT_FALSE(ImageHelper::IsSvgDocument("<svg"));
    EXPECT_FALSE(ImageHelper::IsSvgDocument("<SVG/>"));
    // Constructs that never close leave nothing to find a root after.
    EXPECT_FALSE(ImageHelper::IsSvgDocument("<!-- unterminated <svg/>"));
    EXPECT_FALSE(ImageHelper::IsSvgDocument("<?xml version=\"1.0\" <svg/>"));
    EXPECT_FALSE(ImageHelper::IsSvgDocument("<!DOCTYPE svg [ <!ENTITY a \"b\"> <svg/>"));
}

// The check is on the upload path itself, so no caller can store a raster
// under the vector label — it would be served as image/svg+xml.
TEST(ImageHelperTest, UploadRefusesAPngLabelledSvgAndStoresNothing) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadPngAsSvg", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        TableHelpers::PhotoSupportTables photoSupportTables(databaseHelper);
        photoSupportTables.AddPhotoSupportTable(transaction, "people");

        auto secretsHelper = Secrets::Test::MakeTestSecretsHelper();
        ImageHelper imageHelper(databaseHelper, secretsHelper);

        const std::string_view png("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16);
        const std::vector<char> bytes(png.begin(), png.end());
        UploadResult result = imageHelper.UploadAndAssociatePhoto(
            transaction, "people", 1, bytes, "svg");

        EXPECT_FALSE(result.success);
        EXPECT_EQ(result.errorMessage, "The file is not an SVG document");
        EXPECT_FALSE(imageHelper.HasPhoto(transaction, "people", 1));
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
