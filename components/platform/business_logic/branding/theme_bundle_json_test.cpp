#include "business_logic/branding/theme_bundle_json.h"

#include <set>
#include <string>

#include <gtest/gtest.h>

#include "business_logic/branding/site_content_slots.h"
#include "business_logic/branding/site_theme_tokens.h"
#include "business_logic/branding/theme_bundle_migrations.h"
#include "util/secrets/secret_keys.h"

namespace Branding {
namespace {

ThemeBundle MakeSampleBundle() {
    ThemeBundle bundle;
    bundle.formatVersion = CurrentBundleFormatVersion();
    bundle.name = "Sunrise Studio";
    bundle.description = "Warm palette.";
    bundle.content[std::string(Secrets::kSiteBrowserTitle)] = "Sunrise Studio";
    bundle.content[std::string(Secrets::kSiteLogoUrl)] = "logo.svg";
    bundle.content[std::string(Secrets::kSiteAddressLines)] =
        "2545 152nd Ave NE\nRedmond, WA 98052";
    bundle.content[std::string(Secrets::kSiteSocialLinks)] =
        "Instagram|https://instagram.com/x\nFacebook|https://facebook.com/x";
    bundle.tokens["site_theme_primary"] = "#e8743b";

    BundleFontSource source;
    source.sourceKey = "google";
    source.displayName = "Google Fonts";
    source.baseUrl = "https://fonts.googleapis.com/css2";
    source.querySuffix = "display=swap";
    source.preconnects = {{"https://fonts.googleapis.com", false},
                          {"https://fonts.gstatic.com", true}};
    bundle.fonts.sources.push_back(source);
    return bundle;
}

// ---- the coverage guard ----

// THE test behind "capture all the settings". A table in a design document goes
// stale; this does not. Adding a token to the registry without it appearing in
// the format now fails here rather than being discovered by a studio whose
// theme came back missing a colour.
TEST(ThemeBundleJsonTest, EmitsAKeyForEveryRegisteredTokenAndSlot) {
    const Json::Value json = ThemeBundleToJson(ThemeBundle{});
    const Json::Value& tokens = json["theme"]["tokens"];
    for (const ThemeToken& token : SiteThemeTokens()) {
        const Json::Value* field = nullptr;
        EXPECT_TRUE(tokens.HasChild(token.key, &field))
            << "token missing from the bundle format: " << token.key;
    }
    EXPECT_EQ(tokens.GetChildren().size(), SiteThemeTokens().size());

    const Json::Value& content = json["theme"]["content"];
    for (const ContentSlot& slot : SiteContentSlots()) {
        const Json::Value* field = nullptr;
        EXPECT_TRUE(content.HasChild(slot.key, &field))
            << "content slot missing from the bundle format: " << slot.key;
    }
    // Plus site_logo_url, which is served outside the slot registry but is
    // unquestionably part of a look (OQ-TF4).
    const Json::Value* logo = nullptr;
    EXPECT_TRUE(content.HasChild(Secrets::kSiteLogoUrl, &logo));
    EXPECT_EQ(content.GetChildren().size(), SiteContentSlots().size() + 1);
}

TEST(ThemeBundleJsonTest, EveryExportedKeyIsASiteKey) {
    // The security half: a bundle must never be able to carry a credential.
    // config_secrets holds Square tokens and the SMTP password.
    const Json::Value json = ThemeBundleToJson(ThemeBundle{});
    for (const auto& [key, value] : json["theme"]["content"].GetChildren()) {
        EXPECT_EQ(key.rfind("site_", 0), 0u) << key;
    }
    for (const auto& [key, value] : json["theme"]["tokens"].GetChildren()) {
        EXPECT_EQ(key.rfind("site_", 0), 0u) << key;
    }
}

// ---- the packing conversions ----

TEST(ThemeBundleJsonTest, LinesRoundTripThroughTheirNaturalJsonShape) {
    EXPECT_EQ(UnpackLines("").size(), 0u);
    EXPECT_EQ(PackLines({}), "");

    const std::vector<std::string> lines = {"2545 152nd Ave NE", "Redmond, WA"};
    EXPECT_EQ(UnpackLines(PackLines(lines)), lines);

    // A trailing newline must not grow a blank entry every round trip.
    EXPECT_EQ(UnpackLines("a\nb\n").size(), 2u);
    // CRLF from a Windows admin is repaired, not rejected.
    EXPECT_EQ(UnpackLines("a\r\nb"), (std::vector<std::string>{"a", "b"}));
}

TEST(ThemeBundleJsonTest, SocialLinksRoundTripIncludingTheAwkwardCases) {
    const std::string stored =
        "Instagram|https://instagram.com/x\nFacebook|https://facebook.com/x";
    const auto links = UnpackLabelledLinks(stored);
    ASSERT_EQ(links.size(), 2u);
    EXPECT_EQ(links[0].label, "Instagram");
    EXPECT_EQ(links[0].url, "https://instagram.com/x");
    EXPECT_EQ(PackLabelledLinks(links), stored);

    // A label-only line (no separator) must pack back WITHOUT growing a bar,
    // or the round trip is not an identity.
    const auto bare = UnpackLabelledLinks("Just a label");
    ASSERT_EQ(bare.size(), 1u);
    EXPECT_EQ(bare[0].label, "Just a label");
    EXPECT_EQ(bare[0].url, "");
    EXPECT_EQ(PackLabelledLinks(bare), "Just a label");
}

TEST(ThemeBundleJsonTest, ABarInTheLabelSurvivesTheSplit) {
    // The separator is the FIRST bar, so a URL containing one is safe. A label
    // containing one is not fully recoverable — this pins the actual behaviour
    // rather than pretending otherwise.
    const auto links = UnpackLabelledLinks("Shop|https://x.test/a?b=1|2");
    ASSERT_EQ(links.size(), 1u);
    EXPECT_EQ(links[0].label, "Shop");
    EXPECT_EQ(links[0].url, "https://x.test/a?b=1|2");
    EXPECT_EQ(PackLabelledLinks(links), "Shop|https://x.test/a?b=1|2");
}

// ---- whole-bundle round trip ----

TEST(ThemeBundleJsonTest, BundleSurvivesAJsonRoundTrip) {
    const ThemeBundle original = MakeSampleBundle();
    ThemeBundle parsed;
    ASSERT_EQ(ThemeBundleFromJson(ThemeBundleToJson(original), parsed), "");

    EXPECT_EQ(parsed.name, original.name);
    EXPECT_EQ(parsed.description, original.description);
    EXPECT_EQ(parsed.formatVersion, CurrentBundleFormatVersion());
    // Values come back in STORED form, packing and all.
    EXPECT_EQ(parsed.content[std::string(Secrets::kSiteAddressLines)],
              original.content.at(std::string(Secrets::kSiteAddressLines)));
    EXPECT_EQ(parsed.content[std::string(Secrets::kSiteSocialLinks)],
              original.content.at(std::string(Secrets::kSiteSocialLinks)));
    EXPECT_EQ(parsed.content[std::string(Secrets::kSiteLogoUrl)], "logo.svg");
    EXPECT_EQ(parsed.tokens["site_theme_primary"], "#e8743b");
}

TEST(ThemeBundleJsonTest, MarkdownWithNewlinesAndQuotesSurvives) {
    ThemeBundle bundle;
    const std::string markdown = "# About\n\nWe are **here** — \"really\".\n";
    bundle.content[std::string(Secrets::kSiteAboutMarkdown)] = markdown;
    ThemeBundle parsed;
    ASSERT_EQ(ThemeBundleFromJson(ThemeBundleToJson(bundle), parsed), "");
    EXPECT_EQ(parsed.content[std::string(Secrets::kSiteAboutMarkdown)], markdown);
}

TEST(ThemeBundleJsonTest, FontsRoundTripWithOrderAsTheOrdinal) {
    ThemeBundle bundle;
    BundleFontSource source;
    source.sourceKey = "google";
    source.displayName = "Google Fonts";
    source.baseUrl = "https://fonts.googleapis.com/css2";
    source.querySuffix = "display=swap";
    source.preconnects = {{"https://fonts.googleapis.com", false},
                          {"https://fonts.gstatic.com", true}};
    bundle.fonts.sources.push_back(source);

    BundleFontFamily cdn;
    cdn.family = "Barlow";
    cdn.fallback = "sans-serif";
    cdn.sourceKind = "cdn";
    cdn.sourceKey = "google";
    cdn.spec = "family=Barlow:ital,wght@0,400;0,700";
    BundleFontFamily uploaded;
    uploaded.family = "Studio Sans";
    uploaded.fallback = "serif";
    uploaded.sourceKind = "uploaded";
    uploaded.faces = {{400, "normal", "studiosans-400.woff2"},
                      {700, "normal", "studiosans-700.woff2"}};
    bundle.fonts.families = {cdn, uploaded};

    ThemeBundle parsed;
    ASSERT_EQ(ThemeBundleFromJson(ThemeBundleToJson(bundle), parsed), "");

    ASSERT_EQ(parsed.fonts.sources.size(), 1u);
    EXPECT_EQ(parsed.fonts.sources[0].preconnects.size(), 2u);
    EXPECT_FALSE(parsed.fonts.sources[0].preconnects[0].crossorigin);
    EXPECT_TRUE(parsed.fonts.sources[0].preconnects[1].crossorigin);
    // The semicolons in Google's real spec grammar must survive verbatim.
    ASSERT_EQ(parsed.fonts.families.size(), 2u);
    EXPECT_EQ(parsed.fonts.families[0].spec, cdn.spec);
    // Order preserved — that IS the ordinal.
    EXPECT_EQ(parsed.fonts.families[0].family, "Barlow");
    EXPECT_EQ(parsed.fonts.families[1].family, "Studio Sans");
    ASSERT_EQ(parsed.fonts.families[1].faces.size(), 2u);
    EXPECT_EQ(parsed.fonts.families[1].faces[1].weight, 700);
    EXPECT_EQ(parsed.fonts.families[1].faces[1].file, "studiosans-700.woff2");
}

TEST(ThemeBundleJsonTest, AppSectionsAreCarriedOpaquely) {
    // A bundle from another app must arrive intact so it can be reported as
    // skipped rather than silently lost.
    ThemeBundle bundle;
    bundle.appSections["page_content"] = Json::Value(Json::JsonObject{
        {"home_sections", Json::Value(Json::JsonArray{
            Json::Value(Json::JsonObject{{"kind", Json::Value("hero")}}),
        })},
    });
    ThemeBundle parsed;
    ASSERT_EQ(ThemeBundleFromJson(ThemeBundleToJson(bundle), parsed), "");
    ASSERT_EQ(parsed.appSections.count("page_content"), 1u);
    EXPECT_EQ(parsed.appSections["page_content"]["home_sections"][0]["kind"]
                  .Get<std::string>(), "hero");
}

// ---- refusals ----

TEST(ThemeBundleJsonTest, RefusesSomethingThatIsNotAThemeBundle) {
    ThemeBundle parsed;
    Json::Value notABundle(Json::JsonObject{{"hello", Json::Value("world")}});
    EXPECT_NE(ThemeBundleFromJson(notABundle, parsed), "");

    Json::Value wrongFormat(Json::JsonObject{
        {"format", Json::Value("someone.elses-theme")},
        {"format_version", Json::Value(static_cast<int64_t>(1))},
    });
    EXPECT_NE(ThemeBundleFromJson(wrongFormat, parsed), "");
}

TEST(ThemeBundleJsonTest, SurvivesAHandEditedFileWithTheWrongJsonTypes) {
    // A studio editing a theme by hand will type "700" for a weight and "true"
    // for a flag. That must produce a validation message about the theme, never
    // a std::bad_variant_access out of a JSON getter.
    Json::Value json = ThemeBundleToJson(MakeSampleBundle());
    json["fonts"]["families"] = Json::Value(Json::JsonArray{
        Json::Value(Json::JsonObject{
            {"family", Json::Value("Studio Sans")},
            {"fallback", Json::Value("serif")},
            {"source_kind", Json::Value("uploaded")},
            {"faces", Json::Value(Json::JsonArray{
                Json::Value(Json::JsonObject{
                    {"weight", Json::Value("700")},          // string, not int
                    {"style", Json::Value("normal")},
                    {"file", Json::Value("x.woff2")},
                }),
            })},
        }),
    });
    json["fonts"]["sources"][0]["preconnects"][1]["crossorigin"] =
        Json::Value("true");                                  // string, not bool

    ThemeBundle parsed;
    ASSERT_EQ(ThemeBundleFromJson(json, parsed), "");
    ASSERT_EQ(parsed.fonts.families.size(), 1u);
    EXPECT_EQ(parsed.fonts.families[0].faces[0].weight, 700);
    EXPECT_TRUE(parsed.fonts.sources[0].preconnects[1].crossorigin);
}

TEST(ThemeBundleJsonTest, ReadsTheFormatVersionWithoutParsingTheRest) {
    Json::Value json = ThemeBundleToJson(MakeSampleBundle());
    EXPECT_EQ(ReadBundleFormatVersion(json), CurrentBundleFormatVersion());
    EXPECT_EQ(ReadBundleFormatVersion(Json::Value(Json::JsonObject{})), 0);
}

}  // namespace
}  // namespace Branding
