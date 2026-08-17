#include "site_assets.h"

#include <string>

#include <gtest/gtest.h>

#include "db_schema/site_assets.h"
#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"

namespace TableHelpers {
namespace {

// A one-pixel PNG, so the bytes are a real image rather than a placeholder.
const std::string kPngBytes =
    std::string("\x89PNG\r\n\x1a\n", 8) + std::string("\x00\x01\x02\x03", 4);

TEST(SiteAssetsTest, PutAndReadBackAnAsset) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("PutAsset", [&](Transaction& transaction) {
        SiteAssets assets(testDb.GetDatabaseHelper());
        const int64_t id = assets.PutAsset(transaction, "logo.png", "png", kPngBytes);
        ASSERT_GT(id, 0);

        KeyValueTable row = assets.GetAssetByName(transaction, "logo.png");
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(row.at(std::string(DbSchema::kSiteAssetType)), "png");
        EXPECT_EQ(assets.GetAssetBytes(transaction, "logo.png"), kPngBytes);
    });
}

TEST(SiteAssetsTest, BytesSurviveAnEmbeddedNulAndHighBytes) {
    // Binary crosses the KeyValueTable layer as bytea hex text; a truncation at
    // the first NUL would corrupt every PNG.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AssetBytes", [&](Transaction& transaction) {
        SiteAssets assets(testDb.GetDatabaseHelper());
        const std::string bytes = std::string("\x89PNG\r\n\x1a\n", 8) +
                                  std::string("a\0b\xff\xfe", 5);
        assets.PutAsset(transaction, "hero.png", "png", bytes);
        EXPECT_EQ(assets.GetAssetBytes(transaction, "hero.png"), bytes);
    });
}

TEST(SiteAssetsTest, PuttingTheSameNameTwiceReplacesRatherThanAccumulating) {
    // Importing a theme twice must not grow the table, and the second import's
    // bytes are the ones that should win.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AssetReplace", [&](Transaction& transaction) {
        SiteAssets assets(testDb.GetDatabaseHelper());
        const int64_t first = assets.PutAsset(transaction, "logo.png", "png", kPngBytes);
        const std::string updated = kPngBytes + "more";
        const int64_t second = assets.PutAsset(transaction, "logo.png", "png", updated);

        EXPECT_EQ(first, second);
        EXPECT_EQ(assets.GetAllAssets(transaction).size(), 1u);
        EXPECT_EQ(assets.GetAssetBytes(transaction, "logo.png"), updated);
    });
}

TEST(SiteAssetsTest, ListingDoesNotDragTheBytesAlong) {
    // The manage pages list assets; pulling megabytes to render a list is the
    // mistake this query exists to avoid.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AssetList", [&](Transaction& transaction) {
        SiteAssets assets(testDb.GetDatabaseHelper());
        assets.PutAsset(transaction, "logo.png", "png", kPngBytes);
        KeyValueTableArray rows = assets.GetAllAssets(transaction);
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(rows[0].count(std::string(DbSchema::kSiteAssetBytes)), 0u);
    });
}

TEST(SiteAssetsTest, MissingNamesReadBackEmptyRatherThanThrowing) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AssetMissing", [&](Transaction& transaction) {
        SiteAssets assets(testDb.GetDatabaseHelper());
        EXPECT_TRUE(assets.GetAssetByName(transaction, "nope.png").empty());
        EXPECT_TRUE(assets.GetAssetBytes(transaction, "nope.png").empty());
    });
}

TEST(SiteAssetsTest, DeleteRemovesOnlyTheNamedAsset) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AssetDelete", [&](Transaction& transaction) {
        SiteAssets assets(testDb.GetDatabaseHelper());
        assets.PutAsset(transaction, "logo.png", "png", kPngBytes);
        assets.PutAsset(transaction, "hero.png", "png", kPngBytes);

        assets.DeleteAssetByName(transaction, "logo.png");

        EXPECT_TRUE(assets.GetAssetByName(transaction, "logo.png").empty());
        EXPECT_FALSE(assets.GetAssetByName(transaction, "hero.png").empty());
    });
}

TEST(SiteAssetsTest, AssetsComeBackInNameOrderSoAListingIsStable) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AssetOrder", [&](Transaction& transaction) {
        SiteAssets assets(testDb.GetDatabaseHelper());
        assets.PutAsset(transaction, "zebra.png", "png", kPngBytes);
        assets.PutAsset(transaction, "apple.png", "png", kPngBytes);
        KeyValueTableArray rows = assets.GetAllAssets(transaction);
        ASSERT_EQ(rows.size(), 2u);
        EXPECT_EQ(rows[0].at(std::string(DbSchema::kSiteAssetName)), "apple.png");
    });
}

}  // namespace
}  // namespace TableHelpers
