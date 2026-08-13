#include "business_logic/branding/site_font_inventory.h"

#include <string>

#include <gtest/gtest.h>

#include "db_schema/site_fonts.h"
#include "sql_util/database_access/transaction.h"
#include "sql_util/table_helpers/site_fonts.h"
#include "test/src/util/database_test_helper.h"

namespace Branding {
namespace {

// --- magic bytes (D14) ---

TEST(SiteFontInventoryTest, RecognisesEveryAcceptedFontWrapper) {
    EXPECT_EQ(FontFormatFromMagicBytes("wOF2rest-of-file"), "woff2");
    EXPECT_EQ(FontFormatFromMagicBytes("wOFFrest-of-file"), "woff");
    EXPECT_EQ(FontFormatFromMagicBytes("OTTOrest-of-file"), "otf");
    EXPECT_EQ(FontFormatFromMagicBytes(std::string("\x00\x01\x00\x00rest", 8)), "ttf");
    EXPECT_EQ(FontFormatFromMagicBytes("truerest"), "ttf");
}

TEST(SiteFontInventoryTest, RejectsBytesThatAreNotAFont) {
    // The whole point: an upload named .woff2 that is really something else must
    // never be stored and then served back from our own origin.
    EXPECT_EQ(FontFormatFromMagicBytes(""), "");
    EXPECT_EQ(FontFormatFromMagicBytes("abc"), "");
    EXPECT_EQ(FontFormatFromMagicBytes("<script>alert(1)</script>"), "");
    EXPECT_EQ(FontFormatFromMagicBytes("\x89PNG\r\n"), "");
    EXPECT_EQ(FontFormatFromMagicBytes("GIF89a"), "");
    EXPECT_EQ(FontFormatFromMagicBytes("%PDF-1.7"), "");
    // Right length, wrong tag.
    EXPECT_EQ(FontFormatFromMagicBytes("wOF3"), "");
}

TEST(SiteFontInventoryTest, SizeCapIsDeclaredAndSane) {
    EXPECT_EQ(kMaxFontFaceBytes, 5u * 1024u * 1024u);
}

// --- URL / name / spec validation (D12) ---

TEST(SiteFontInventoryTest, SourceUrlRequiresHttps) {
    EXPECT_TRUE(IsValidFontSourceUrl("https://fonts.googleapis.com/css2"));
    EXPECT_TRUE(IsValidFontSourceUrl("https://fonts.gstatic.com"));
    // A tenant must not be able to downgrade its own visitors.
    EXPECT_FALSE(IsValidFontSourceUrl("http://fonts.googleapis.com/css2"));
    EXPECT_FALSE(IsValidFontSourceUrl("//fonts.googleapis.com"));
    EXPECT_FALSE(IsValidFontSourceUrl("javascript:alert(1)"));
    EXPECT_FALSE(IsValidFontSourceUrl(""));
    EXPECT_FALSE(IsValidFontSourceUrl("https://"));
}

TEST(SiteFontInventoryTest, SourceUrlRejectsAttributeBreakingCharacters) {
    // These land in an href attribute the boot code injects.
    EXPECT_FALSE(IsValidFontSourceUrl("https://x.test/\"><script>"));
    EXPECT_FALSE(IsValidFontSourceUrl("https://x.test/a b"));
    EXPECT_FALSE(IsValidFontSourceUrl("https://x.test/a\nb"));
}

TEST(SiteFontInventoryTest, FamilyNameIsAPlainName) {
    EXPECT_TRUE(IsValidFontFamilyName("Barlow"));
    EXPECT_TRUE(IsValidFontFamilyName("Open Sans"));
    EXPECT_TRUE(IsValidFontFamilyName("Din-Condensed_Bold"));
    EXPECT_FALSE(IsValidFontFamilyName("Barlow', sans-serif; x:y"));
    EXPECT_FALSE(IsValidFontFamilyName("Barlow\""));
    EXPECT_FALSE(IsValidFontFamilyName(""));
}

TEST(SiteFontInventoryTest, SpecAcceptsTheRealGoogleFontsGrammar) {
    EXPECT_TRUE(IsValidFontSpec("family=Barlow:wght@100..900"));
    EXPECT_TRUE(IsValidFontSpec("display=swap"));
    // The semicolon separates axis tuples and IS load-bearing — the app's own
    // shipped index.html font URL uses exactly this form.
    EXPECT_TRUE(IsValidFontSpec("family=Roboto:ital,wght@0,300;0,400;0,500;0,700;1,400"));
    EXPECT_TRUE(IsValidFontSpec("family=Barlow:wght@600;700"));
    // '+' encodes a space in a family name; '%' allows percent-encoding.
    EXPECT_TRUE(IsValidFontSpec("family=Open+Sans:wght@400"));
    EXPECT_TRUE(IsValidFontSpec("family=Noto%20Sans"));
}

TEST(SiteFontInventoryTest, SpecCannotSmuggleASecondParameter) {
    // A row contributes ONE parameter; '&' would let it become a different
    // request than the one the admin thought they were configuring.
    EXPECT_FALSE(IsValidFontSpec("family=Barlow&family=Evil"));
    EXPECT_FALSE(IsValidFontSpec("family=Barlow#x"));
    EXPECT_FALSE(IsValidFontSpec(""));
}

// --- URL construction (Mason's worked example) ---

TEST(SiteFontInventoryTest, BuildsOneUrlPerSourceWithSpecsJoined) {
    // This is the exact URL from the design conversation.
    std::string url = BuildFontStylesheetUrl(
        "https://fonts.googleapis.com/css2",
        {"family=Barlow:wght@100..900", "family=Roboto:wght@100..900"},
        "display=swap");
    EXPECT_EQ(url,
              "https://fonts.googleapis.com/css2"
              "?family=Barlow:wght@100..900&family=Roboto:wght@100..900"
              "&display=swap");
}

TEST(SiteFontInventoryTest, BuildsWithoutASuffixWhenTheSourceHasNone) {
    EXPECT_EQ(BuildFontStylesheetUrl(
                  "https://fonts.example/css", {"family=A"}, ""),
              "https://fonts.example/css?family=A");
}

TEST(SiteFontInventoryTest, BuildReturnsNothingWhenThereIsNothingValidToAskFor) {
    EXPECT_EQ(BuildFontStylesheetUrl("https://fonts.example/css", {}, ""), "");
    EXPECT_EQ(BuildFontStylesheetUrl("http://insecure.test", {"family=A"}, ""), "");
    // Every spec junk — asking for nothing is worse than not asking at all.
    EXPECT_EQ(BuildFontStylesheetUrl(
                  "https://fonts.example/css", {"family=A&evil=1"}, ""), "");
}

TEST(SiteFontInventoryTest, BuildSkipsAJunkSpecButKeepsTheGoodOnes) {
    EXPECT_EQ(BuildFontStylesheetUrl(
                  "https://fonts.example/css",
                  {"family=Good", "family=Bad&x=1", "family=AlsoGood"}, ""),
              "https://fonts.example/css?family=Good&family=AlsoGood");
}

// --- preconnect lines ---

TEST(SiteFontInventoryTest, ParsesPreconnectLinesWithTheCrossoriginFlag) {
    auto preconnects = ParsePreconnectLines(
        "https://fonts.googleapis.com|false\n"
        "https://fonts.gstatic.com|true");
    ASSERT_EQ(preconnects.size(), 2u);
    EXPECT_EQ(preconnects[0].href, "https://fonts.googleapis.com");
    EXPECT_FALSE(preconnects[0].crossorigin);
    EXPECT_EQ(preconnects[1].href, "https://fonts.gstatic.com");
    EXPECT_TRUE(preconnects[1].crossorigin);
}

TEST(SiteFontInventoryTest, PreconnectLinesToleratePlainHrefsAndBlankRows) {
    auto preconnects = ParsePreconnectLines(
        "\nhttps://a.test\n\n  https://b.test|true  \n");
    ASSERT_EQ(preconnects.size(), 2u);
    EXPECT_EQ(preconnects[0].href, "https://a.test");
    EXPECT_FALSE(preconnects[0].crossorigin);
    EXPECT_TRUE(preconnects[1].crossorigin);
}

TEST(SiteFontInventoryTest, PreconnectLinesDropInvalidHrefs) {
    auto preconnects = ParsePreconnectLines(
        "http://insecure.test|false\njavascript:alert(1)\nhttps://ok.test");
    ASSERT_EQ(preconnects.size(), 1u);
    EXPECT_EQ(preconnects[0].href, "https://ok.test");
}

// --- stack composition (D13) ---

TEST(SiteFontInventoryTest, ComposesAQuotedFamilyWithItsFallback) {
    EXPECT_EQ(ComposeFontStack("Barlow", "sans-serif"), "'Barlow', sans-serif");
    EXPECT_EQ(ComposeFontStack("Open Sans", "Arial, sans-serif"),
              "'Open Sans', Arial, sans-serif");
}

TEST(SiteFontInventoryTest, NeverInventsAFallbackWhenTheRowHasNone) {
    // Mason's "I don't want to assume sanserif" — the fallback is data, and its
    // absence is honoured rather than papered over.
    EXPECT_EQ(ComposeFontStack("Barlow", ""), "'Barlow'");
}

TEST(SiteFontInventoryTest, ComposeRefusesAFamilyThatWouldEscapeTheDeclaration) {
    EXPECT_EQ(ComposeFontStack("Barlow'; color:red", "sans-serif"), "");
}

// --- the whole inventory, against the database ---

TEST(SiteFontInventoryTest, BuildsTheGoogleShapedInventoryFromRows) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontInventory", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        TableHelpers::SiteFonts fonts(databaseHelper);

        int64_t google = fonts.AddSource(
            transaction, "google", "Google Fonts",
            "https://fonts.googleapis.com/css2", "display=swap",
            "https://fonts.googleapis.com|false\nhttps://fonts.gstatic.com|true");
        fonts.AddFont(transaction, "Barlow", "sans-serif",
                      DbSchema::kSiteFontSourceKindCdn, google,
                      "family=Barlow:wght@100..900", 10);
        fonts.AddFont(transaction, "Roboto", "sans-serif",
                      DbSchema::kSiteFontSourceKindCdn, google,
                      "family=Roboto:wght@100..900", 20);

        SiteFontInventory inventory = LoadSiteFontInventory(
            databaseHelper, transaction, "/api/site_font_face/");

        // ONE stylesheet for the source, not one per font.
        ASSERT_EQ(inventory.stylesheets.size(), 1u);
        EXPECT_EQ(inventory.stylesheets[0],
                  "https://fonts.googleapis.com/css2"
                  "?family=Barlow:wght@100..900&family=Roboto:wght@100..900"
                  "&display=swap");
        ASSERT_EQ(inventory.preconnects.size(), 2u);
        EXPECT_TRUE(inventory.preconnects[1].crossorigin);
        EXPECT_TRUE(inventory.faces.empty());
    });
}

TEST(SiteFontInventoryTest, SystemFamiliesNeedNoSourceAndProduceNoRequest) {
    // D13's third kind: "Georgia, serif" with nothing to fetch.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontSystem", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        TableHelpers::SiteFonts fonts(databaseHelper);
        fonts.AddFont(transaction, "Georgia", "serif",
                      DbSchema::kSiteFontSourceKindSystem, 0, "", 10);

        SiteFontInventory inventory = LoadSiteFontInventory(
            databaseHelper, transaction, "/api/site_font_face/");
        EXPECT_TRUE(inventory.stylesheets.empty());
        EXPECT_TRUE(inventory.preconnects.empty());
        EXPECT_TRUE(inventory.faces.empty());

        EXPECT_EQ(LookupFontStack(databaseHelper, transaction, "Georgia"),
                  "'Georgia', serif");
    });
}

TEST(SiteFontInventoryTest, UploadedFacesBecomeDescriptorsPointingAtTheEndpoint) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontFaces", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        TableHelpers::SiteFonts fonts(databaseHelper);

        int64_t font = fonts.AddFont(
            transaction, "Studio Sans", "sans-serif",
            DbSchema::kSiteFontSourceKindUploaded, 0, "", 10);
        int64_t faceId = fonts.AddFace(
            transaction, font, 700, "normal", "woff2", "wOF2fake-bytes");

        SiteFontInventory inventory = LoadSiteFontInventory(
            databaseHelper, transaction, "/api/site_font_face/");
        ASSERT_EQ(inventory.faces.size(), 1u);
        EXPECT_EQ(inventory.faces[0].family, "Studio Sans");
        EXPECT_EQ(inventory.faces[0].weight, 700);
        EXPECT_EQ(inventory.faces[0].style, "normal");
        EXPECT_EQ(inventory.faces[0].format, "woff2");
        EXPECT_EQ(inventory.faces[0].url,
                  "/api/site_font_face/" + std::to_string(faceId));
        // An uploaded family makes no CDN request.
        EXPECT_TRUE(inventory.stylesheets.empty());
    });
}

TEST(SiteFontInventoryTest, AnUnusedSourceCostsAVisitorNothing) {
    // A configured-but-unreferenced source must not emit a preconnect: that
    // would open a connection to a third party for no reason.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontUnusedSource", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        TableHelpers::SiteFonts fonts(databaseHelper);
        fonts.AddSource(transaction, "unused", "Unused",
                        "https://unused.test/css", "", "https://unused.test|true");

        SiteFontInventory inventory = LoadSiteFontInventory(
            databaseHelper, transaction, "/api/site_font_face/");
        EXPECT_TRUE(inventory.stylesheets.empty());
        EXPECT_TRUE(inventory.preconnects.empty());
    });
}

TEST(SiteFontInventoryTest, LookupFontStackIsEmptyForAFamilyWithNoRow) {
    // Which is what makes site_info drop the role rather than emit a bare name.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontLookupMiss", [&](Transaction& transaction) {
        EXPECT_EQ(LookupFontStack(
                      testDb.GetDatabaseHelper(), transaction, "Nonexistent"), "");
    });
}

}  // namespace
}  // namespace Branding
