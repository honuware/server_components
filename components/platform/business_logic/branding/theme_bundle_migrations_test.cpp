#include "business_logic/branding/theme_bundle_migrations.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "business_logic/branding/theme_bundle.h"

namespace Branding {
namespace {

Json::Value BundleAtVersion(int version) {
    return Json::Value(Json::JsonObject{
        {"format", Json::Value(std::string(kThemeBundleFormat))},
        {"format_version", Json::Value(static_cast<int64_t>(version))},
        {"theme", Json::Value(Json::JsonObject{
            {"content", Json::Value(Json::JsonObject{})},
            {"tokens", Json::Value(Json::JsonObject{})},
        })},
    });
}

// The chain being contiguous is a BUILD property. Discovering a gap at runtime
// would mean a studio's import failing for a reason it cannot act on, so it is
// asserted here instead.
TEST(ThemeBundleMigrationsTest, TheChainIsContiguousUpToTheCurrentVersion) {
    int version = OldestSupportedBundleFormatVersion();
    for (const BundleMigration& migration : BundleMigrations()) {
        EXPECT_EQ(migration.from, version)
            << "migration chain has a gap before " << migration.description;
        EXPECT_GT(migration.to, migration.from) << migration.description;
        EXPECT_FALSE(migration.description.empty())
            << "every migration needs a line the studio can read";
        EXPECT_TRUE(static_cast<bool>(migration.apply));
        version = migration.to;
    }
    EXPECT_EQ(version, CurrentBundleFormatVersion())
        << "the chain does not reach the current format version";
    EXPECT_LE(OldestSupportedBundleFormatVersion(), CurrentBundleFormatVersion());
}

TEST(ThemeBundleMigrationsTest, ACurrentBundleIsLeftAlone) {
    Json::Value json = BundleAtVersion(CurrentBundleFormatVersion());
    std::vector<std::string> applied;
    EXPECT_EQ(MigrateBundleJson(json, applied), "");
    EXPECT_TRUE(applied.empty());
    EXPECT_EQ(json["format_version"].Get<int64_t>(),
              CurrentBundleFormatVersion());
}

TEST(ThemeBundleMigrationsTest, RefusesABundleFromANewerBuild) {
    // Applying only the parts we recognise would produce a look its author
    // never approved, so this is refused rather than partially honoured.
    Json::Value json = BundleAtVersion(CurrentBundleFormatVersion() + 1);
    std::vector<std::string> applied;
    const std::string error = MigrateBundleJson(json, applied);
    EXPECT_NE(error, "");
    EXPECT_NE(error.find("newer"), std::string::npos);
}

TEST(ThemeBundleMigrationsTest, RefusesABundleWithNoVersionAtAll) {
    Json::Value json = Json::Value(Json::JsonObject{
        {"format", Json::Value(std::string(kThemeBundleFormat))},
    });
    std::vector<std::string> applied;
    EXPECT_NE(MigrateBundleJson(json, applied), "");
}

TEST(ThemeBundleMigrationsTest, RefusesABundleOlderThanTheChainReaches) {
    Json::Value json = BundleAtVersion(OldestSupportedBundleFormatVersion() - 1);
    std::vector<std::string> applied;
    EXPECT_NE(MigrateBundleJson(json, applied), "");
}

TEST(ThemeBundleMigrationsTest, AVersionTypedAsAStringIsStillRead) {
    // A hand-edited file will do this.
    Json::Value json = BundleAtVersion(CurrentBundleFormatVersion());
    json["format_version"] = Json::Value(std::to_string(CurrentBundleFormatVersion()));
    std::vector<std::string> applied;
    EXPECT_EQ(MigrateBundleJson(json, applied), "");
}

// A worked example of what a step looks like, so the first real rename is an
// append rather than a design exercise. Runs the migration mechanism directly
// rather than through the (currently empty) production chain.
TEST(ThemeBundleMigrationsTest, AMigrationStepRenamesAKeyInPlace) {
    BundleMigration rename{
        1, 2, "site_theme_brand → site_theme_primary",
        [](Json::Value& json) {
            Json::Value* tokens = nullptr;
            if (!json.HasChild("theme", &tokens)) return;
            Json::Value* inner = nullptr;
            if (!tokens->HasChild("tokens", &inner)) return;
            Json::Value* old = nullptr;
            if (!inner->HasChild("site_theme_brand", &old)) return;
            (*inner)["site_theme_primary"] = *old;
            inner->GetChildren().erase("site_theme_brand");
        }};

    Json::Value json = BundleAtVersion(1);
    json["theme"]["tokens"]["site_theme_brand"] = Json::Value("#ed1c26");
    rename.apply(json);

    const Json::Value& tokens = json["theme"]["tokens"];
    const Json::Value* migrated = nullptr;
    ASSERT_TRUE(tokens.HasChild("site_theme_primary", &migrated));
    EXPECT_EQ(migrated->Get<std::string>(), "#ed1c26");
    const Json::Value* gone = nullptr;
    EXPECT_FALSE(tokens.HasChild("site_theme_brand", &gone));
}

// ---- the report ----

TEST(ThemeBundleMigrationsTest, TheReportSaysWhatWasSkippedAndWhy) {
    BundleImportReport report;
    report.ok = true;
    report.migratedFrom = 1;
    report.migrationsApplied = {"site_theme_brand → site_theme_primary"};
    report.unknownKeys = {"site_theme_nonsense"};
    report.skippedSections = {"page_content"};
    report.contentChanges = 12;
    report.tokenChanges = 47;
    report.fontFamilyChanges = 3;
    report.assetChanges = 5;

    const Json::Value json = BundleImportReportToJson(report);
    EXPECT_TRUE(json["ok"].Get<bool>());
    EXPECT_EQ(json["migrated_from"].Get<int64_t>(), 1);
    EXPECT_EQ(json["migrations_applied"].GetArray().size(), 1u);
    // Lenient must never be silent — the skipped keys have to reach the studio.
    EXPECT_EQ(json["unknown_keys"][0].Get<std::string>(), "site_theme_nonsense");
    EXPECT_EQ(json["skipped_sections"][0].Get<std::string>(), "page_content");
    EXPECT_EQ(json["changes"]["tokens"].Get<int64_t>(), 47);
}

TEST(ThemeBundleMigrationsTest, AFailedReportCarriesItsReason) {
    BundleImportReport report;
    report.ok = false;
    report.error = "site_theme_nonsense is not a setting this site has.";
    const Json::Value json = BundleImportReportToJson(report);
    EXPECT_FALSE(json["ok"].Get<bool>());
    EXPECT_EQ(json["error"].Get<std::string>(), report.error);
}

}  // namespace
}  // namespace Branding
