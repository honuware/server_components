#include "business_logic/branding/theme_bundle_sections.h"

#include <string>

#include <gtest/gtest.h>

namespace Branding {
namespace {

SectionExporter NoopExporter(const std::string& marker) {
    return [marker](SectionContext&, Json::Value& out) {
        out = Json::Value(Json::JsonObject{{"marker", Json::Value(marker)}});
        return std::string();
    };
}

SectionImporter NoopImporter() {
    return [](SectionContext&, const Json::Value&, bool) { return std::string(); };
}

TEST(ThemeBundleSectionsTest, RegistersAndFindsASection) {
    ClearThemeBundleSectionsForTest();
    RegisterThemeBundleSection("page_content", NoopExporter("a"), NoopImporter());

    ASSERT_EQ(ThemeBundleSections().size(), 1u);
    const ThemeBundleSection* found = FindThemeBundleSection("page_content");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "page_content");
    ClearThemeBundleSectionsForTest();
}

TEST(ThemeBundleSectionsTest, AnUnregisteredNameIsNotAnError) {
    // This IS the cross-app case: a CommunityFinder theme carries sections
    // Knotty Yoga has no tables for. The caller reports them as skipped rather
    // than failing the import, which is what makes colours and fonts portable
    // between apps.
    ClearThemeBundleSectionsForTest();
    EXPECT_EQ(FindThemeBundleSection("page_content"), nullptr);
}

TEST(ThemeBundleSectionsTest, RegisteringTwiceReplacesRatherThanDuplicates) {
    // Two registrations of one name would export the section twice and import
    // it twice; a test installing a stub would leak into the next test.
    ClearThemeBundleSectionsForTest();
    RegisterThemeBundleSection("page_content", NoopExporter("first"), NoopImporter());
    RegisterThemeBundleSection("page_content", NoopExporter("second"), NoopImporter());

    ASSERT_EQ(ThemeBundleSections().size(), 1u);
    SectionContext context;
    Json::Value out;
    EXPECT_EQ(ThemeBundleSections()[0].exporter(context, out), "");
    EXPECT_EQ(out["marker"].Get<std::string>(), "second");
    ClearThemeBundleSectionsForTest();
}

TEST(ThemeBundleSectionsTest, SectionsKeepRegistrationOrder) {
    // Order matters: it is the order sections export and import in, so a
    // bundle's section order is stable across runs and a round trip compares
    // byte-for-byte.
    ClearThemeBundleSectionsForTest();
    RegisterThemeBundleSection("alpha", NoopExporter("a"), NoopImporter());
    RegisterThemeBundleSection("beta", NoopExporter("b"), NoopImporter());
    RegisterThemeBundleSection("alpha", NoopExporter("a2"), NoopImporter());

    ASSERT_EQ(ThemeBundleSections().size(), 2u);
    EXPECT_EQ(ThemeBundleSections()[0].name, "alpha");
    EXPECT_EQ(ThemeBundleSections()[1].name, "beta");
    ClearThemeBundleSectionsForTest();
}

TEST(ThemeBundleSectionsTest, AnExporterCanFailTheWholeExport) {
    ClearThemeBundleSectionsForTest();
    RegisterThemeBundleSection(
        "broken",
        [](SectionContext&, Json::Value&) {
            return std::string("home_sections could not be read");
        },
        NoopImporter());

    SectionContext context;
    Json::Value out;
    EXPECT_EQ(ThemeBundleSections()[0].exporter(context, out),
              "home_sections could not be read");
    ClearThemeBundleSectionsForTest();
}

TEST(ThemeBundleSectionsTest, AnImporterSeesTheMergeFlag) {
    // A section has to honour the same replace-vs-patch contract the framework
    // half does, or --merge would mean two different things in one file.
    ClearThemeBundleSectionsForTest();
    bool sawMerge = false;
    RegisterThemeBundleSection(
        "page_content", NoopExporter("a"),
        [&sawMerge](SectionContext&, const Json::Value&, bool merge) {
            sawMerge = merge;
            return std::string();
        });

    SectionContext context;
    ThemeBundleSections()[0].importer(context, Json::Value(), true);
    EXPECT_TRUE(sawMerge);
    ClearThemeBundleSectionsForTest();
}

}  // namespace
}  // namespace Branding
