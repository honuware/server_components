#include "business_logic/branding/theme_bundle_export.h"
#include "business_logic/branding/theme_bundle_import.h"

#include <string>

#include <gtest/gtest.h>

#include "business_logic/branding/theme_bundle_json.h"
#include "business_logic/branding/theme_bundle_sections.h"
#include "db_schema/site_fonts.h"
#include "sql_util/table_helpers/site_assets.h"
#include "sql_util/table_helpers/site_fonts.h"
#include "test/src/util/database_test_helper.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"

namespace Branding {
namespace {

// Real magic bytes, because both halves decide what a file IS from its content.
const std::string kPng = std::string("\x89PNG\r\n\x1a\n", 8) + "pretend-image";
const std::string kWoff2 = std::string("wOF2") + "pretend-font-bytes";

ThemeBundleExportOptions Options() {
    ThemeBundleExportOptions options;
    options.name = "Sunrise Studio";
    options.description = "Warm palette.";
    // Passed in rather than read from a clock, so a bundle is reproducible.
    options.exportedAt = "2026-08-17T18:22:04Z";
    options.app = "knottyyoga";
    options.site = "test";
    options.honuwareVersion = "test";
    return options;
}

// Puts a recognisable look into the tenant: a couple of slots, a token, a CDN
// family, an uploaded family with a real face, and a logo image.
void SeedALook(
    Transaction& transaction,
    TestDatabaseUtil& testDb,
    Secrets::SecretsHelper& secrets) {
    secrets.AddSecret(transaction, Secrets::kSiteBrowserTitle, "Sunrise Studio");
    secrets.AddSecret(transaction, Secrets::kSiteAddressLines,
                      "2545 152nd Ave NE\nRedmond, WA 98052");
    secrets.AddSecret(transaction, Secrets::kSiteSocialLinks,
                      "Instagram|https://instagram.com/x");
    secrets.AddSecret(transaction, "site_theme_primary", "#e8743b");
    secrets.AddSecret(transaction, "site_theme_radius_card", "12px");

    TableHelpers::SiteAssets assets(testDb.GetDatabaseHelper());
    assets.PutAsset(transaction, "logo.png", "png", kPng);
    secrets.AddSecret(transaction, Secrets::kSiteLogoUrl, "/api/site_asset/logo.png");

    TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
    const int64_t sourceId = fonts.AddSource(
        transaction, "google", "Google Fonts",
        "https://fonts.googleapis.com/css2", "display=swap",
        "https://fonts.googleapis.com|false\nhttps://fonts.gstatic.com|true");
    fonts.AddFont(transaction, "Barlow", "sans-serif",
                  DbSchema::kSiteFontSourceKindCdn, sourceId,
                  "family=Barlow:ital,wght@0,400;0,700", 10);
    const int64_t uploaded = fonts.AddFont(
        transaction, "Studio Sans", "serif",
        DbSchema::kSiteFontSourceKindUploaded, 0, "", 20);
    fonts.AddFace(transaction, uploaded, 700, "normal", "woff2", kWoff2);
}

// ---- THE acceptance test ----

TEST(ThemeBundleRoundTripTest, ExportImportExportIsByteIdentical) {
    // Every ambiguity in the format shows up here, which is why this is the
    // acceptance test rather than a checklist. If a value packs one way and
    // unpacks another, or a filename is derived non-deterministically, the two
    // documents differ.
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleRoundTrip", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedALook(transaction, testDb, *secrets);

        ThemeBundle first;
        ASSERT_EQ(ExportThemeBundle(testDb.GetDatabaseHelper(), transaction,
                                    *secrets, Options(), first), "");
        const std::string firstJson = ThemeBundleToJson(first).ToString();

        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(first), first.assets, ThemeBundleImportOptions{});
        ASSERT_TRUE(report.ok) << report.error;

        ThemeBundle second;
        ASSERT_EQ(ExportThemeBundle(testDb.GetDatabaseHelper(), transaction,
                                    *secrets, Options(), second), "");

        EXPECT_EQ(ThemeBundleToJson(second).ToString(), firstJson);
        // The assets have to match byte for byte too — a theme whose logo came
        // back subtly different would be a silently wrong round trip.
        ASSERT_EQ(second.assets.size(), first.assets.size());
        for (const auto& [name, bytes] : first.assets) {
            ASSERT_EQ(second.assets.count(name), 1u) << name;
            EXPECT_EQ(second.assets.at(name), bytes) << name;
        }
    });
}

TEST(ThemeBundleRoundTripTest, TheExportedLookIsWhatWasSeeded) {
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleContents", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedALook(transaction, testDb, *secrets);

        ThemeBundle bundle;
        ASSERT_EQ(ExportThemeBundle(testDb.GetDatabaseHelper(), transaction,
                                    *secrets, Options(), bundle), "");

        EXPECT_EQ(bundle.content[std::string(Secrets::kSiteBrowserTitle)],
                  "Sunrise Studio");
        EXPECT_EQ(bundle.tokens["site_theme_primary"], "#e8743b");
        // A site asset comes out as the BARE filename, with its bytes attached —
        // that is what makes the theme portable rather than a link home.
        EXPECT_EQ(bundle.content[std::string(Secrets::kSiteLogoUrl)], "logo.png");
        ASSERT_EQ(bundle.assets.count("logo.png"), 1u);
        EXPECT_EQ(bundle.assets["logo.png"], kPng);

        ASSERT_EQ(bundle.fonts.families.size(), 2u);
        EXPECT_EQ(bundle.fonts.families[0].family, "Barlow");
        EXPECT_EQ(bundle.fonts.families[0].sourceKey, "google");
        ASSERT_EQ(bundle.fonts.families[1].faces.size(), 1u);
        EXPECT_EQ(bundle.assets[bundle.fonts.families[1].faces[0].file], kWoff2);
    });
}

TEST(ThemeBundleRoundTripTest, ImportingIntoABlankTenantReproducesTheLook) {
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleApply", [&](Transaction& transaction) {
        auto source = Secrets::Test::MakeTestSecretsHelper();
        SeedALook(transaction, testDb, *source);
        ThemeBundle bundle;
        ASSERT_EQ(ExportThemeBundle(testDb.GetDatabaseHelper(), transaction,
                                    *source, Options(), bundle), "");
        const Json::Value json = ThemeBundleToJson(bundle);

        // A fresh secrets store stands in for a blank tenant.
        auto blank = Secrets::Test::MakeTestSecretsHelper();
        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *blank, json,
            bundle.assets, ThemeBundleImportOptions{});
        ASSERT_TRUE(report.ok) << report.error;

        EXPECT_EQ(blank->LookupSecret(transaction, Secrets::kSiteBrowserTitle),
                  "Sunrise Studio");
        EXPECT_EQ(blank->LookupSecret(transaction, "site_theme_primary"), "#e8743b");
        // The bundled filename became a servable URL again.
        EXPECT_EQ(blank->LookupSecret(transaction, Secrets::kSiteLogoUrl),
                  "/api/site_asset/logo.png");
        TableHelpers::SiteAssets assets(testDb.GetDatabaseHelper());
        EXPECT_EQ(assets.GetAssetBytes(transaction, "logo.png"), kPng);
    });
}

// ---- replace vs merge (OQ-TF2) ----

TEST(ThemeBundleRoundTripTest, ReplaceResetsATokenTheBundleDoesNotMention) {
    // The decision that makes "try alternatives" work: without it, flipping
    // between two themes accumulates the union of both and neither look is
    // what its author intended.
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleReplace", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, "site_theme_accent", "#123456");

        ThemeBundle bundle;
        bundle.formatVersion = 1;
        bundle.tokens["site_theme_primary"] = "#e8743b";
        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(bundle), {}, ThemeBundleImportOptions{});
        ASSERT_TRUE(report.ok) << report.error;

        EXPECT_EQ(secrets->LookupSecret(transaction, "site_theme_primary"), "#e8743b");
        EXPECT_EQ(secrets->LookupSecret(transaction, "site_theme_accent"), "")
            << "a token the bundle omitted must go back to its default";
    });
}

TEST(ThemeBundleRoundTripTest, MergeLeavesWhatItDoesNotMention) {
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleMerge", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, "site_theme_accent", "#123456");

        ThemeBundle bundle;
        bundle.formatVersion = 1;
        bundle.tokens["site_theme_primary"] = "#e8743b";
        Json::Value json = ThemeBundleToJson(bundle);
        // ThemeBundleToJson emits every registered key; a merge payload is the
        // subset a caller actually sends, so trim to just the one.
        json["theme"]["tokens"] = Json::Value(Json::JsonObject{
            {"site_theme_primary", Json::Value("#e8743b")},
        });
        json["theme"]["content"] = Json::Value(Json::JsonObject{});

        ThemeBundleImportOptions options;
        options.merge = true;
        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets, json, {}, options);
        ASSERT_TRUE(report.ok) << report.error;

        EXPECT_EQ(secrets->LookupSecret(transaction, "site_theme_primary"), "#e8743b");
        EXPECT_EQ(secrets->LookupSecret(transaction, "site_theme_accent"), "#123456");
    });
}

// ---- refusals ----

TEST(ThemeBundleRoundTripTest, ADryRunWritesNothing) {
    // This is what makes trying a theme safe — you see the consequences before
    // anything changes.
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleDryRun", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        ThemeBundle bundle;
        bundle.formatVersion = 1;
        bundle.tokens["site_theme_primary"] = "#e8743b";

        ThemeBundleImportOptions options;
        options.dryRun = true;
        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(bundle), {}, options);

        ASSERT_TRUE(report.ok) << report.error;
        EXPECT_GT(report.tokenChanges, 0) << "a dry run still reports what it would do";
        EXPECT_EQ(secrets->LookupSecret(transaction, "site_theme_primary"), "");
    });
}

TEST(ThemeBundleRoundTripTest, RefusesAFontFileThatIsNotAFont) {
    // D14, through the bundle path: a renamed .woff2 that is really HTML must
    // not be stored and then served back from our own origin.
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleFakeFont", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        ThemeBundle bundle;
        bundle.formatVersion = 1;
        BundleFontFamily family;
        family.family = "Studio Sans";
        family.fallback = "serif";
        family.sourceKind = "uploaded";
        family.faces = {{400, "normal", "studiosans.woff2"}};
        bundle.fonts.families.push_back(family);

        std::map<std::string, std::string> assets{
            {"studiosans.woff2", "<html>not a font at all</html>"}};
        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(bundle), assets, ThemeBundleImportOptions{});

        EXPECT_FALSE(report.ok);
        EXPECT_NE(report.error.find("not an image or a font"), std::string::npos);
    });
}

TEST(ThemeBundleRoundTripTest, RefusesAReferenceToAnAssetThatIsNotThere) {
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleMissingAsset", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        ThemeBundle bundle;
        bundle.formatVersion = 1;
        bundle.content[std::string(Secrets::kSiteLogoUrl)] = "logo.png";

        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(bundle), {}, ThemeBundleImportOptions{});

        EXPECT_FALSE(report.ok);
        EXPECT_NE(report.error.find("logo.png"), std::string::npos);
    });
}

TEST(ThemeBundleRoundTripTest, RefusesAKeyThatIsNotASiteSettingUnderBothModes) {
    // The credential guard. config_secrets holds Square tokens and the SMTP
    // password; "lenient" must never become the way one arrives in a "theme".
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleCredential", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        ThemeBundle bundle;
        bundle.formatVersion = 1;
        // Injected into the JSON rather than the struct on purpose: the
        // EXPORTER is registry-driven and physically cannot emit an unknown
        // key, so a hostile bundle can only ever arrive from outside. This is
        // the incoming path, which is the one that needs the guard.
        Json::Value json = ThemeBundleToJson(bundle);
        json["theme"]["content"]["mail_app_password"] = Json::Value("hunter2");
        const std::string before =
            secrets->LookupSecret(transaction, "mail_app_password");

        for (BundleStrictness strictness :
             {BundleStrictness::Strict, BundleStrictness::Lenient}) {
            ThemeBundleImportOptions options;
            options.strictness = strictness;
            BundleImportReport report = ImportThemeBundleJson(
                testDb.GetDatabaseHelper(), transaction, *secrets, json, {}, options);
            EXPECT_FALSE(report.ok);
        }
        // Unchanged — the credential the tenant already had is untouched, and
        // the bundle's value never landed.
        EXPECT_EQ(secrets->LookupSecret(transaction, "mail_app_password"), before);
        EXPECT_NE(secrets->LookupSecret(transaction, "mail_app_password"), "hunter2");
    });
}

TEST(ThemeBundleRoundTripTest, StrictRefusesAnUnknownSiteKeyAndLenientReportsIt) {
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleUnknown", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        ThemeBundle bundle;
        bundle.formatVersion = 1;
        bundle.tokens["site_theme_primary"] = "#e8743b";
        // Injected into the JSON: an unknown key can only arrive from a file
        // written by a different build, never from our own exporter.
        Json::Value json = ThemeBundleToJson(bundle);
        json["theme"]["tokens"]["site_theme_from_the_future"] = Json::Value("#000000");

        ThemeBundleImportOptions strict;
        BundleImportReport refused = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets, json, {}, strict);
        EXPECT_FALSE(refused.ok);
        EXPECT_NE(refused.error.find("site_theme_from_the_future"), std::string::npos);

        ThemeBundleImportOptions lenient;
        lenient.strictness = BundleStrictness::Lenient;
        BundleImportReport applied = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets, json, {}, lenient);
        ASSERT_TRUE(applied.ok) << applied.error;
        // Lenient is never SILENT — the studio has to be told what was dropped.
        ASSERT_EQ(applied.unknownKeys.size(), 1u);
        EXPECT_EQ(applied.unknownKeys[0], "site_theme_from_the_future");
        EXPECT_EQ(secrets->LookupSecret(transaction, "site_theme_primary"), "#e8743b");
    });
}

TEST(ThemeBundleRoundTripTest, RefusesAJunkTokenValueRatherThanStoringIt) {
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleJunkValue", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        ThemeBundle bundle;
        bundle.formatVersion = 1;
        bundle.tokens["site_theme_primary"] = "red; background: url(evil)";

        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(bundle), {}, ThemeBundleImportOptions{});
        EXPECT_FALSE(report.ok);
        EXPECT_EQ(secrets->LookupSecret(transaction, "site_theme_primary"), "");
    });
}

TEST(ThemeBundleRoundTripTest, RefusesAFamilyNamingAServiceTheBundleDoesNotCarry) {
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleDanglingSource", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        ThemeBundle bundle;
        bundle.formatVersion = 1;
        BundleFontFamily family;
        family.family = "Barlow";
        family.fallback = "sans-serif";
        family.sourceKind = "cdn";
        family.sourceKey = "google";
        family.spec = "family=Barlow";
        bundle.fonts.families.push_back(family);

        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(bundle), {}, ThemeBundleImportOptions{});
        EXPECT_FALSE(report.ok);
    });
}

// ---- app sections ----

TEST(ThemeBundleRoundTripTest, ASectionThisAppDoesNotKnowIsReportedNotFatal) {
    // The cross-app case: a CommunityFinder theme's colours and fonts stay
    // usable here even though Knotty Yoga has no tables for its sections.
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleForeignSection", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        ThemeBundle bundle;
        bundle.formatVersion = 1;
        bundle.tokens["site_theme_primary"] = "#e8743b";
        bundle.appSections["listings_page"] =
            Json::Value(Json::JsonObject{{"rows", Json::Value(Json::JsonArray{})}});

        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(bundle), {}, ThemeBundleImportOptions{});

        ASSERT_TRUE(report.ok) << report.error;
        ASSERT_EQ(report.skippedSections.size(), 1u);
        EXPECT_EQ(report.skippedSections[0], "listings_page");
        // The framework half still applied.
        EXPECT_EQ(secrets->LookupSecret(transaction, "site_theme_primary"), "#e8743b");
    });
}

TEST(ThemeBundleRoundTripTest, ARegisteredSectionRoundTrips) {
    ClearThemeBundleSectionsForTest();
    RegisterThemeBundleSection(
        "page_content",
        [](SectionContext&, Json::Value& out) {
            out = Json::Value(Json::JsonObject{{"rows", Json::Value("two")}});
            return std::string();
        },
        [](SectionContext&, const Json::Value& body, bool) {
            return body["rows"].Get<std::string>() == "two"
                       ? std::string()
                       : std::string("unexpected body");
        });

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleSection", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        ThemeBundle bundle;
        ASSERT_EQ(ExportThemeBundle(testDb.GetDatabaseHelper(), transaction,
                                    *secrets, Options(), bundle), "");
        ASSERT_EQ(bundle.appSections.count("page_content"), 1u);

        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(bundle), bundle.assets, ThemeBundleImportOptions{});
        ASSERT_TRUE(report.ok) << report.error;
        EXPECT_TRUE(report.skippedSections.empty());
    });
    ClearThemeBundleSectionsForTest();
}

TEST(ThemeBundleRoundTripTest, ASectionFailureFailsTheWholeImport) {
    // The caller runs this in one transaction, so a section refusing must take
    // the framework half down with it rather than leaving half a theme.
    ClearThemeBundleSectionsForTest();
    RegisterThemeBundleSection(
        "page_content",
        [](SectionContext&, Json::Value& out) {
            out = Json::Value(Json::JsonObject{});
            return std::string();
        },
        [](SectionContext&, const Json::Value&, bool) {
            return std::string("home section 2 has an unknown kind");
        });

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleSectionFails", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        ThemeBundle bundle;
        bundle.formatVersion = 1;
        bundle.appSections["page_content"] = Json::Value(Json::JsonObject{});

        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(bundle), {}, ThemeBundleImportOptions{});
        EXPECT_FALSE(report.ok);
        EXPECT_EQ(report.error, "home section 2 has an unknown kind");
    });
    ClearThemeBundleSectionsForTest();
}

// ---- Tolerance: applying what fits instead of refusing the file -------------
//
// Mason, 8/24/2026: "the theme files have always been flaky and give no help
// debugging issues. If extra things are there, ignore them. If things are
// missing, apply the changes that are there."

TEST(ThemeBundleRoundTripTest, LenientAppliesTheGoodHalfAndReportsTheBad) {
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleLenientPrune", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        ThemeBundle bundle;
        bundle.formatVersion = 1;
        // Good.
        bundle.content[std::string(Secrets::kSiteBrowserTitle)] = "Sunrise Studio";
        bundle.tokens["site_theme_primary"] = "#e8743b";
        // Bad: a logo pointing at a file the zip does not carry, and a token
        // whose value is junk.
        bundle.content[std::string(Secrets::kSiteLogoUrl)] = "logo.png";
        bundle.tokens["site_theme_accent"] = "not-a-colour";

        ThemeBundleImportOptions options;
        options.strictness = BundleStrictness::Lenient;
        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(bundle), {}, options);

        // The import SUCCEEDS — that is the whole point.
        EXPECT_TRUE(report.ok) << report.error;
        EXPECT_EQ(secrets->LookupSecret(transaction, Secrets::kSiteBrowserTitle),
                  "Sunrise Studio");
        EXPECT_EQ(secrets->LookupSecret(transaction, "site_theme_primary"),
                  "#e8743b");
        // The bad token was dropped rather than stored.
        EXPECT_NE(secrets->LookupSecret(transaction, "site_theme_accent"),
                  "not-a-colour");

        // ...and every skipped item is attributed.
        ASSERT_EQ(report.problems.size(), 2u);
        bool sawLogo = false;
        bool sawAccent = false;
        for (const BundleProblem& problem : report.problems) {
            if (problem.item == Secrets::kSiteLogoUrl) {
                sawLogo = true;
                EXPECT_EQ(problem.area, "content");
                EXPECT_NE(problem.reason.find("logo.png"), std::string::npos);
            }
            if (problem.item == "site_theme_accent") {
                sawAccent = true;
                EXPECT_EQ(problem.area, "tokens");
                EXPECT_NE(problem.reason.find("not-a-colour"), std::string::npos);
            }
        }
        EXPECT_TRUE(sawLogo);
        EXPECT_TRUE(sawAccent);
    });
}

// One unusable family must not cost the studio the rest of their fonts.
TEST(ThemeBundleRoundTripTest, LenientKeepsTheGoodFontsAndDropsTheBrokenOne) {
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleLenientFonts", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        ThemeBundle bundle;
        bundle.formatVersion = 1;

        BundleFontFamily good;
        good.family = "Studio Sans";
        good.fallback = "sans-serif";
        good.sourceKind = "uploaded";
        good.faces = {{400, "normal", "studiosans.woff2"}};
        bundle.fonts.families.push_back(good);

        // Its file never travelled, so the family has nothing to render with.
        BundleFontFamily broken;
        broken.family = "Missing Face";
        broken.fallback = "serif";
        broken.sourceKind = "uploaded";
        broken.faces = {{400, "normal", "gone.woff2"}};
        bundle.fonts.families.push_back(broken);

        ThemeBundleImportOptions options;
        options.strictness = BundleStrictness::Lenient;
        std::map<std::string, std::string> assets{{"studiosans.woff2", kWoff2}};
        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(bundle), assets, options);

        EXPECT_TRUE(report.ok) << report.error;
        TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
        EXPECT_FALSE(fonts.GetFontByFamily(transaction, "Studio Sans").empty());
        EXPECT_TRUE(fonts.GetFontByFamily(transaction, "Missing Face").empty());
        // TWO problems, deliberately: the face that could not be placed (naming
        // the file, which is what a studio needs in order to fix it) and the
        // family that was left with nothing to render. Reporting only the
        // second would say "no usable font files" without saying which.
        bool sawFace = false;
        bool sawFamily = false;
        for (const BundleProblem& problem : report.problems) {
            if (problem.item != "Missing Face") {
                continue;
            }
            EXPECT_EQ(problem.area, "fonts");
            if (problem.reason.find("gone.woff2") != std::string::npos) {
                sawFace = true;
            }
            if (problem.reason.find("no usable font files") != std::string::npos) {
                sawFamily = true;
            }
        }
        EXPECT_TRUE(sawFace) << "the missing file has to be named";
        EXPECT_TRUE(sawFamily) << "and so does the family it cost";
    });
}

// THE BUG THIS WORK CAME FROM (Mason, 8/24/2026). A database provisioned
// before site_assets existed made every theme import 500 with a SQL fragment in
// the log and nothing on screen.
//
// The images are in the zip, and a studio pressing Apply has said everything
// they need to say — so the import CREATES the table it needs rather than
// dropping the pictures and telling them to go and run a command-line tool.
TEST(ThemeBundleRoundTripTest, AMissingAssetTableIsCreatedAndTheImagesLand) {
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleNoAssetTable", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        // Reproduce the older database. The harness pre-creates every table, so
        // it has to be dropped to test anything at all.
        transaction.RunSqlStatement("DROP TABLE IF EXISTS site_assets");

        ThemeBundle bundle;
        bundle.formatVersion = 1;
        bundle.content[std::string(Secrets::kSiteBrowserTitle)] = "Sunrise Studio";
        bundle.content[std::string(Secrets::kSiteLogoUrl)] = "logo.png";

        ThemeBundleImportOptions options;
        options.strictness = BundleStrictness::Lenient;
        std::map<std::string, std::string> assets{{"logo.png", kPng}};
        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(bundle), assets, options);

        ASSERT_TRUE(report.ok) << report.error;
        // NOTHING was skipped — the point of the change.
        EXPECT_TRUE(report.problems.empty())
            << "the images are in the file; they should have been stored";
        EXPECT_EQ(report.assetChanges, 1);

        // The image really is stored, read back through the production reader.
        TableHelpers::SiteAssets siteAssets(testDb.GetDatabaseHelper());
        EXPECT_EQ(siteAssets.GetAssetBytes(transaction, "logo.png"), kPng);
        // ...and the logo slot points at it rather than falling back.
        EXPECT_EQ(secrets->LookupSecret(transaction, Secrets::kSiteLogoUrl),
                  "/api/site_asset/logo.png");
    });
}

// The font tables get the same treatment, in foreign-key order — a family with
// nowhere to put its faces is not half-usable, it is unusable.
TEST(ThemeBundleRoundTripTest, MissingFontTablesAreCreatedAndTheFontsLand) {
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleNoFontTables", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        // Dropped children-first, the reverse of the creation order.
        transaction.RunSqlStatement("DROP TABLE IF EXISTS site_font_faces");
        transaction.RunSqlStatement("DROP TABLE IF EXISTS site_fonts");
        transaction.RunSqlStatement("DROP TABLE IF EXISTS site_font_sources");

        ThemeBundle bundle;
        bundle.formatVersion = 1;
        BundleFontFamily family;
        family.family = "Studio Sans";
        family.fallback = "serif";
        family.sourceKind = "uploaded";
        family.faces = {{700, "normal", "studiosans.woff2"}};
        bundle.fonts.families.push_back(family);

        ThemeBundleImportOptions options;
        options.strictness = BundleStrictness::Lenient;
        std::map<std::string, std::string> assets{{"studiosans.woff2", kWoff2}};
        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(bundle), assets, options);

        ASSERT_TRUE(report.ok) << report.error;
        EXPECT_TRUE(report.problems.empty());
        TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
        EXPECT_FALSE(fonts.GetFontByFamily(transaction, "Studio Sans").empty());
    });
}

// Creating a table is a WRITE, and a dry run writes nothing. The apply does it
// before touching either area, so nothing is lost by waiting.
TEST(ThemeBundleRoundTripTest, ADryRunCreatesNoTables) {
    ClearThemeBundleSectionsForTest();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleDryRunNoDdl", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        transaction.RunSqlStatement("DROP TABLE IF EXISTS site_assets");

        ThemeBundle bundle;
        bundle.formatVersion = 1;
        bundle.content[std::string(Secrets::kSiteLogoUrl)] = "logo.png";

        ThemeBundleImportOptions options;
        options.strictness = BundleStrictness::Lenient;
        options.dryRun = true;
        std::map<std::string, std::string> assets{{"logo.png", kPng}};
        BundleImportReport report = ImportThemeBundleJson(
            testDb.GetDatabaseHelper(), transaction, *secrets,
            ThemeBundleToJson(bundle), assets, options);

        EXPECT_TRUE(report.ok) << report.error;
        EXPECT_EQ(
            transaction.RunSqlStatementReturningOneValue(
                "SELECT COUNT(*) FROM information_schema.tables "
                "WHERE table_name = 'site_assets'"),
            "0");
    });
}

}  // namespace
}  // namespace Branding
