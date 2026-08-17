#include "business_logic/branding/theme_bundle_zip.h"

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <zip.h>

#include "business_logic/branding/theme_bundle_assets.h"
#include "business_logic/branding/theme_bundle_json.h"
#include "business_logic/branding/theme_bundle_migrations.h"
#include "util/secrets/secret_keys.h"

namespace Branding {
namespace {

const std::string kPng = std::string("\x89PNG\r\n\x1a\n", 8) + "pretend-image";

ThemeBundle SampleBundle() {
    ThemeBundle bundle;
    bundle.formatVersion = CurrentBundleFormatVersion();
    bundle.name = "Sunrise Studio";
    bundle.content[std::string(Secrets::kSiteLogoUrl)] = "logo.png";
    bundle.tokens["site_theme_primary"] = "#e8743b";
    bundle.assets["logo.png"] = kPng;
    return bundle;
}

TEST(ThemeBundleZipTest, WriterAndReaderRoundTrip) {
    const std::string zip = ThemeBundleToZip(SampleBundle());
    ASSERT_FALSE(zip.empty());
    // A real zip, not something that happens to be bytes.
    EXPECT_EQ(zip.compare(0, 2, "PK"), 0);

    Json::Value json;
    std::map<std::string, std::string> assets;
    ASSERT_EQ(ThemeBundleFromZip(zip, json, assets), "");

    ThemeBundle parsed;
    ASSERT_EQ(ThemeBundleFromJson(json, parsed), "");
    EXPECT_EQ(parsed.name, "Sunrise Studio");
    EXPECT_EQ(parsed.tokens["site_theme_primary"], "#e8743b");
    // Binary must survive byte for byte — a PNG with a mangled header is a
    // broken logo, not a slightly different one.
    ASSERT_EQ(assets.count("logo.png"), 1u);
    EXPECT_EQ(assets["logo.png"], kPng);
}

TEST(ThemeBundleZipTest, ThemeJsonIsTheFirstEntry) {
    // So a human opening the archive sees what it is, rather than a list of
    // images with the manifest buried among them.
    const std::string zip = ThemeBundleToZip(SampleBundle());
    ASSERT_FALSE(zip.empty());
    // The first local file header's name follows the 30-byte fixed header.
    ASSERT_GT(zip.size(), 30u + kThemeBundleJsonName.size());
    EXPECT_EQ(zip.compare(30, kThemeBundleJsonName.size(),
                          std::string(kThemeBundleJsonName)), 0);
}

TEST(ThemeBundleZipTest, RefusesAnEntryThatIsAPath) {
    // Zip-slip, the whole reason the archive is flat. Built by hand because our
    // own writer cannot produce one.
    ThemeBundle bundle = SampleBundle();
    bundle.assets.clear();
    bundle.assets["../evil.png"] = kPng;
    // The writer refuses to make it in the first place.
    EXPECT_TRUE(ThemeBundleToZip(bundle).empty());
}

TEST(ThemeBundleZipTest, RefusesSomethingThatIsNotAZip) {
    Json::Value json;
    std::map<std::string, std::string> assets;
    EXPECT_NE(ThemeBundleFromZip("this is not a zip file", json, assets), "");
    EXPECT_NE(ThemeBundleFromZip("", json, assets), "");
}

TEST(ThemeBundleZipTest, RefusesATruncatedArchiveRatherThanCrashing) {
    const std::string zip = ThemeBundleToZip(SampleBundle());
    ASSERT_GT(zip.size(), 40u);
    Json::Value json;
    std::map<std::string, std::string> assets;
    // Half a file is what a dropped upload looks like.
    EXPECT_NE(ThemeBundleFromZip(zip.substr(0, zip.size() / 2), json, assets), "");
}

// Builds an archive with arbitrary entry names, which our own writer refuses to
// produce. Needed to test the READER against the shapes an attacker would send.
std::string MakeRawZip(
    const std::vector<std::pair<std::string, std::string>>& entries) {
    zip_error_t error;
    zip_error_init(&error);
    zip_source_t* source = zip_source_buffer_create(nullptr, 0, 0, &error);
    if (!source) return {};
    zip_source_keep(source);
    zip_t* archive = zip_open_from_source(source, ZIP_TRUNCATE, &error);
    if (!archive) { zip_source_free(source); return {}; }

    std::vector<std::string> held;
    held.reserve(entries.size());
    for (const auto& [name, bytes] : entries) {
        held.push_back(bytes);
        zip_source_t* entry =
            zip_source_buffer(archive, held.back().data(), held.back().size(), 0);
        zip_file_add(archive, name.c_str(), entry, ZIP_FL_ENC_UTF_8);
    }
    zip_close(archive);

    std::string result;
    if (zip_source_open(source) == 0) {
        char buffer[16384];
        for (;;) {
            const zip_int64_t read = zip_source_read(source, buffer, sizeof(buffer));
            if (read <= 0) break;
            result.append(buffer, static_cast<std::size_t>(read));
        }
        zip_source_close(source);
    }
    zip_source_free(source);
    return result;
}

TEST(ThemeBundleZipTest, ReaderRefusesAnEntryThatIsAPath) {
    // Zip-slip from the READER's side — the side that takes untrusted input.
    // The writer test above only proves we do not CREATE one.
    for (const char* name : {"../evil.png", "sub/dir.png", "/etc/passwd"}) {
        const std::string zip = MakeRawZip({
            {std::string(kThemeBundleJsonName), R"({"format":"honuware.site-theme"})"},
            {name, kPng},
        });
        ASSERT_FALSE(zip.empty()) << name;
        Json::Value json;
        std::map<std::string, std::string> assets;
        EXPECT_NE(ThemeBundleFromZip(zip, json, assets), "") << name;
    }
}

TEST(ThemeBundleZipTest, ReaderRefusesAnArchiveWithNoThemeJson) {
    const std::string zip = MakeRawZip({{"logo.png", kPng}});
    ASSERT_FALSE(zip.empty());
    Json::Value json;
    std::map<std::string, std::string> assets;
    const std::string reason = ThemeBundleFromZip(zip, json, assets);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("theme.json"), std::string::npos);
}

TEST(ThemeBundleZipTest, ReaderRefusesTooManyEntries) {
    std::vector<std::pair<std::string, std::string>> entries{
        {std::string(kThemeBundleJsonName), R"({"format":"honuware.site-theme"})"}};
    for (std::size_t i = 0; i <= kMaxBundleAssets + 1; ++i) {
        entries.push_back({"asset" + std::to_string(i) + ".png", kPng});
    }
    const std::string zip = MakeRawZip(entries);
    ASSERT_FALSE(zip.empty());
    Json::Value json;
    std::map<std::string, std::string> assets;
    EXPECT_NE(ThemeBundleFromZip(zip, json, assets), "");
}

TEST(ThemeBundleZipTest, ReaderRefusesAnOversizedEntryBeforeExpandingIt) {
    // A zip bomb is refused on its DECLARED size, not after being expanded into
    // memory and then measured.
    std::string big = std::string("\x89PNG\r\n\x1a\n", 8);
    big.append(kMaxBundleAssetBytes + 1024, 'x');
    const std::string zip = MakeRawZip({
        {std::string(kThemeBundleJsonName), R"({"format":"honuware.site-theme"})"},
        {"huge.png", big},
    });
    ASSERT_FALSE(zip.empty());
    Json::Value json;
    std::map<std::string, std::string> assets;
    const std::string reason = ThemeBundleFromZip(zip, json, assets);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("larger"), std::string::npos);
}

TEST(ThemeBundleZipTest, AnEmptyBundleStillProducesAReadableArchive) {
    ThemeBundle bundle;
    bundle.formatVersion = CurrentBundleFormatVersion();
    const std::string zip = ThemeBundleToZip(bundle);
    ASSERT_FALSE(zip.empty());

    Json::Value json;
    std::map<std::string, std::string> assets;
    ASSERT_EQ(ThemeBundleFromZip(zip, json, assets), "");
    EXPECT_TRUE(assets.empty());
    ThemeBundle parsed;
    EXPECT_EQ(ThemeBundleFromJson(json, parsed), "");
}

TEST(ThemeBundleZipTest, LargeButLegalAssetsSurvive) {
    // Well under the cap, but past any single-read buffer size — a reader that
    // only ever fetched the first chunk would pass every test above and corrupt
    // every real font.
    ThemeBundle bundle = SampleBundle();
    std::string big = std::string("\x89PNG\r\n\x1a\n", 8);
    big.append(300000, 'x');
    bundle.assets["hero.png"] = big;

    const std::string zip = ThemeBundleToZip(bundle);
    ASSERT_FALSE(zip.empty());
    Json::Value json;
    std::map<std::string, std::string> assets;
    ASSERT_EQ(ThemeBundleFromZip(zip, json, assets), "");
    EXPECT_EQ(assets["hero.png"], big);
}

}  // namespace
}  // namespace Branding
