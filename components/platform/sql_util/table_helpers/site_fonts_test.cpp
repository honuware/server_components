#include "site_fonts.h"

#include <string>

#include <gtest/gtest.h>

#include "db_schema/site_fonts.h"
#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"

namespace TableHelpers {
namespace {

TEST(SiteFontsTest, AddAndGetSource) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddSource", [&](Transaction& transaction) {
        SiteFonts fonts(testDb.GetDatabaseHelper());
        int64_t id = fonts.AddSource(
            transaction, "google", "Google Fonts",
            "https://fonts.googleapis.com/css2", "display=swap",
            "https://fonts.googleapis.com|false\nhttps://fonts.gstatic.com|true");
        ASSERT_GT(id, 0);

        KeyValueTable row = fonts.GetSource(transaction, id);
        EXPECT_EQ(row.at("source_key"), "google");
        EXPECT_EQ(row.at("display_name"), "Google Fonts");
        EXPECT_EQ(row.at("base_url"), "https://fonts.googleapis.com/css2");
        EXPECT_EQ(row.at("query_suffix"), "display=swap");
        EXPECT_EQ(row.at("active"), "t");
    });
}

TEST(SiteFontsTest, SourceKeyIsUniqueSoAFontReferenceIsUnambiguous) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SourceKeyUnique", [&](Transaction& transaction) {
        SiteFonts fonts(testDb.GetDatabaseHelper());
        fonts.AddSource(transaction, "google", "A", "https://a.test", "", "");
        EXPECT_ANY_THROW(
            fonts.AddSource(transaction, "google", "B", "https://b.test", "", ""));
    });
}

TEST(SiteFontsTest, GetSourceByKeyFindsTheRow) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SourceByKey", [&](Transaction& transaction) {
        SiteFonts fonts(testDb.GetDatabaseHelper());
        fonts.AddSource(transaction, "google", "Google Fonts",
                        "https://fonts.googleapis.com/css2", "display=swap", "");
        KeyValueTable row = fonts.GetSourceByKey(transaction, "google");
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(row.at("display_name"), "Google Fonts");
        EXPECT_TRUE(fonts.GetSourceByKey(transaction, "nope").empty());
    });
}

TEST(SiteFontsTest, AddAndGetFontCarriesFamilyAndFallbackSeparately) {
    // D13: two columns, neither ever defaulted.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddFont", [&](Transaction& transaction) {
        SiteFonts fonts(testDb.GetDatabaseHelper());
        int64_t source = fonts.AddSource(
            transaction, "google", "Google Fonts",
            "https://fonts.googleapis.com/css2", "display=swap", "");
        int64_t id = fonts.AddFont(
            transaction, "Barlow", "sans-serif",
            DbSchema::kSiteFontSourceKindCdn, source,
            "family=Barlow:wght@100..900", 10);
        ASSERT_GT(id, 0);

        KeyValueTable row = fonts.GetFont(transaction, id);
        EXPECT_EQ(row.at("family"), "Barlow");
        EXPECT_EQ(row.at("fallback"), "sans-serif");
        EXPECT_EQ(row.at("source_kind"), "cdn");
        EXPECT_EQ(row.at("spec"), "family=Barlow:wght@100..900");
        EXPECT_EQ(row.at("font_source_id"), std::to_string(source));
        EXPECT_EQ(row.at("active"), "t");
    });
}

TEST(SiteFontsTest, ASystemFamilyStoresNoSourceRatherThanADanglingZero) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SystemFont", [&](Transaction& transaction) {
        SiteFonts fonts(testDb.GetDatabaseHelper());
        int64_t id = fonts.AddFont(
            transaction, "Georgia", "serif",
            DbSchema::kSiteFontSourceKindSystem, 0, "", 10);
        KeyValueTable row = fonts.GetFont(transaction, id);
        EXPECT_EQ(row.at("source_kind"), "system");
        // NULL, not 0 — a source id of zero would look like a real reference.
        EXPECT_EQ(row.at("font_source_id"), "");
    });
}

TEST(SiteFontsTest, FontsComeBackInOrdinalOrderSoTheUrlIsStable) {
    // A churning stylesheet URL would miss the CDN cache on every page load.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontOrder", [&](Transaction& transaction) {
        SiteFonts fonts(testDb.GetDatabaseHelper());
        fonts.AddFont(transaction, "Third", "serif",
                      DbSchema::kSiteFontSourceKindSystem, 0, "", 30);
        fonts.AddFont(transaction, "First", "serif",
                      DbSchema::kSiteFontSourceKindSystem, 0, "", 10);
        fonts.AddFont(transaction, "Second", "serif",
                      DbSchema::kSiteFontSourceKindSystem, 0, "", 20);

        auto rows = fonts.GetActiveFonts(transaction);
        ASSERT_EQ(rows.size(), 3u);
        EXPECT_EQ(rows[0].at("family"), "First");
        EXPECT_EQ(rows[1].at("family"), "Second");
        EXPECT_EQ(rows[2].at("family"), "Third");
    });
}

TEST(SiteFontsTest, GetFontByFamilyResolvesARoleTokensName) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontByFamily", [&](Transaction& transaction) {
        SiteFonts fonts(testDb.GetDatabaseHelper());
        fonts.AddFont(transaction, "Barlow", "sans-serif",
                      DbSchema::kSiteFontSourceKindSystem, 0, "", 10);
        EXPECT_EQ(fonts.GetFontByFamily(transaction, "Barlow").at("fallback"),
                  "sans-serif");
        EXPECT_TRUE(fonts.GetFontByFamily(transaction, "Missing").empty());
    });
}

TEST(SiteFontsTest, FaceBytesRoundTripThroughByteaWithoutTruncatingAtANul) {
    // Font files are full of NULs; a C-string round trip would truncate.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FaceBytes", [&](Transaction& transaction) {
        SiteFonts fonts(testDb.GetDatabaseHelper());
        int64_t font = fonts.AddFont(
            transaction, "Studio Sans", "sans-serif",
            DbSchema::kSiteFontSourceKindUploaded, 0, "", 10);

        std::string bytes("wOF2\x00\x01\x00\xFF binary \x00 tail", 26);
        int64_t faceId = fonts.AddFace(
            transaction, font, 700, "italic", "woff2", bytes);
        ASSERT_GT(faceId, 0);

        EXPECT_EQ(fonts.GetFaceBytes(transaction, faceId), bytes);

        KeyValueTable face = fonts.GetFace(transaction, faceId);
        EXPECT_EQ(face.at("weight"), "700");
        EXPECT_EQ(face.at("style"), "italic");
        EXPECT_EQ(face.at("format"), "woff2");
        // The descriptor read must NOT drag the file into memory.
        EXPECT_EQ(face.count("bytes"), 0u);
    });
}

TEST(SiteFontsTest, FacesForFontComeBackWithoutTheirBytes) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FacesForFont", [&](Transaction& transaction) {
        SiteFonts fonts(testDb.GetDatabaseHelper());
        int64_t font = fonts.AddFont(
            transaction, "Studio Sans", "sans-serif",
            DbSchema::kSiteFontSourceKindUploaded, 0, "", 10);
        fonts.AddFace(transaction, font, 700, "normal", "woff2", "wOF2bold");
        fonts.AddFace(transaction, font, 400, "normal", "woff2", "wOF2regular");

        auto faces = fonts.GetFacesForFont(transaction, font);
        ASSERT_EQ(faces.size(), 2u);
        EXPECT_EQ(faces[0].at("weight"), "400");
        EXPECT_EQ(faces[1].at("weight"), "700");
        EXPECT_EQ(faces[0].count("bytes"), 0u);
    });
}

TEST(SiteFontsTest, AllActiveFacesJoinsTheFamilyForTheFontFaceRule) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AllActiveFaces", [&](Transaction& transaction) {
        SiteFonts fonts(testDb.GetDatabaseHelper());
        int64_t font = fonts.AddFont(
            transaction, "Studio Sans", "sans-serif",
            DbSchema::kSiteFontSourceKindUploaded, 0, "", 10);
        fonts.AddFace(transaction, font, 400, "normal", "woff2", "wOF2a");

        auto faces = fonts.GetAllActiveFaces(transaction);
        ASSERT_EQ(faces.size(), 1u);
        EXPECT_EQ(faces[0].at("family"), "Studio Sans");
        EXPECT_EQ(faces[0].at("fallback"), "sans-serif");
        EXPECT_EQ(faces[0].at("format"), "woff2");
    });
}

TEST(SiteFontsTest, DeleteRemovesOnlyTheTargetRow) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontDelete", [&](Transaction& transaction) {
        SiteFonts fonts(testDb.GetDatabaseHelper());
        int64_t keep = fonts.AddFont(transaction, "Keep", "serif",
                                     DbSchema::kSiteFontSourceKindSystem, 0, "", 10);
        int64_t drop = fonts.AddFont(transaction, "Drop", "serif",
                                     DbSchema::kSiteFontSourceKindSystem, 0, "", 20);
        fonts.DeleteFont(transaction, drop);

        EXPECT_FALSE(fonts.GetFont(transaction, keep).empty());
        EXPECT_TRUE(fonts.GetFont(transaction, drop).empty());
    });
}

}  // namespace
}  // namespace TableHelpers
