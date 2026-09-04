#include "business_logic/branding/site_info_app_blocks.h"

#include <string>

#include <gtest/gtest.h>

#include "test/src/util/database_test_helper.h"

namespace Branding {
namespace {

// Polish Phase 12.2 — the seam that lets an APP contribute to /api/site_info
// without honuware growing a field it cannot explain.

TEST(SiteInfoAppBlocksTest, RegistersAndRunsABlock) {
    ClearSiteInfoBlocksForTest();
    RegisterSiteInfoBlock("providers", [](Transaction&, DatabaseHelper) {
        return Json::Value(Json::JsonObject{{"has_any", Json::Value(true)}});
    });

    ASSERT_EQ(SiteInfoBlocks().size(), 1u);
    EXPECT_EQ(SiteInfoBlocks()[0].name, "providers");

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SiteInfoBlock", [&](Transaction& transaction) {
        Json::Value value =
            SiteInfoBlocks()[0].build(transaction, testDb.GetDatabaseHelper());
        EXPECT_TRUE(value["has_any"].Get<bool>());
    });
    ClearSiteInfoBlocksForTest();
}

// Idempotent by name, the same rule RegisterThemeBundleSection follows. Two
// registrations of one name would otherwise both run, and a test installing a
// stub would leak into the next test.
TEST(SiteInfoAppBlocksTest, RegisteringTheSameNameReplacesRatherThanDuplicates) {
    ClearSiteInfoBlocksForTest();
    RegisterSiteInfoBlock("providers", [](Transaction&, DatabaseHelper) {
        return Json::Value(Json::JsonObject{{"has_any", Json::Value(false)}});
    });
    RegisterSiteInfoBlock("providers", [](Transaction&, DatabaseHelper) {
        return Json::Value(Json::JsonObject{{"has_any", Json::Value(true)}});
    });

    ASSERT_EQ(SiteInfoBlocks().size(), 1u);
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SiteInfoBlockReplace", [&](Transaction& transaction) {
        // The SECOND registration wins.
        EXPECT_TRUE(SiteInfoBlocks()[0]
                        .build(transaction, testDb.GetDatabaseHelper())["has_any"]
                        .Get<bool>());
    });
    ClearSiteInfoBlocksForTest();
}

TEST(SiteInfoAppBlocksTest, KeepsRegistrationOrderAcrossNames) {
    ClearSiteInfoBlocksForTest();
    RegisterSiteInfoBlock("first", [](Transaction&, DatabaseHelper) {
        return Json::Value(Json::JsonObject{});
    });
    RegisterSiteInfoBlock("second", [](Transaction&, DatabaseHelper) {
        return Json::Value(Json::JsonObject{});
    });

    ASSERT_EQ(SiteInfoBlocks().size(), 2u);
    EXPECT_EQ(SiteInfoBlocks()[0].name, "first");
    EXPECT_EQ(SiteInfoBlocks()[1].name, "second");
    ClearSiteInfoBlocksForTest();
}

// A framework-only consumer (CommunityFinder registers nothing today) must not
// be a special case anywhere.
TEST(SiteInfoAppBlocksTest, NoBlocksIsAValidState) {
    ClearSiteInfoBlocksForTest();
    EXPECT_TRUE(SiteInfoBlocks().empty());
}

}  // namespace
}  // namespace Branding
