#include "business_logic/branding/theme_bundle_directory.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "business_logic/branding/theme_bundle_json.h"
#include "business_logic/branding/theme_bundle_migrations.h"
#include "util/secrets/secret_keys.h"

namespace Branding {
namespace {

namespace fs = std::filesystem;

const std::string kPng = std::string("\x89PNG\r\n\x1a\n", 8) + "pretend-image";

// A scratch directory that cleans itself up however the test exits.
class ScratchDir {
public:
    explicit ScratchDir(const std::string& name)
        : path_(fs::temp_directory_path() / ("theme-bundle-test-" + name)) {
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }
    ~ScratchDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    std::string Path() const { return path_.string(); }
    fs::path Native() const { return path_; }

private:
    fs::path path_;
};

ThemeBundle SampleBundle() {
    ThemeBundle bundle;
    bundle.formatVersion = CurrentBundleFormatVersion();
    bundle.name = "Sunrise Studio";
    bundle.content[std::string(Secrets::kSiteLogoUrl)] = "logo.png";
    bundle.tokens["site_theme_primary"] = "#e8743b";
    bundle.assets["logo.png"] = kPng;
    return bundle;
}

TEST(ThemeBundleDirectoryTest, WritesTheJsonAndItsImagesSideBySide) {
    // The shape Mason asked for: a filename in the json, the file beside it.
    ScratchDir dir("write");
    ASSERT_EQ(WriteThemeBundleDirectory(SampleBundle(), dir.Path(), false), "");

    EXPECT_TRUE(fs::exists(dir.Native() / "theme.json"));
    EXPECT_TRUE(fs::exists(dir.Native() / "logo.png"));
}

TEST(ThemeBundleDirectoryTest, RoundTripsThroughTheDisk) {
    ScratchDir dir("roundtrip");
    ASSERT_EQ(WriteThemeBundleDirectory(SampleBundle(), dir.Path(), false), "");

    Json::Value json;
    std::map<std::string, std::string> assets;
    ASSERT_EQ(ReadThemeBundleDirectory(dir.Path(), json, assets), "");

    ThemeBundle parsed;
    ASSERT_EQ(ThemeBundleFromJson(json, parsed), "");
    EXPECT_EQ(parsed.name, "Sunrise Studio");
    EXPECT_EQ(parsed.tokens["site_theme_primary"], "#e8743b");
    // Binary must survive the filesystem byte for byte.
    ASSERT_EQ(assets.count("logo.png"), 1u);
    EXPECT_EQ(assets["logo.png"], kPng);
}

TEST(ThemeBundleDirectoryTest, RefusesToClobberAnExistingThemeWithoutForce) {
    // Overwriting a theme because someone mistyped a path is not recoverable.
    ScratchDir dir("clobber");
    ASSERT_EQ(WriteThemeBundleDirectory(SampleBundle(), dir.Path(), false), "");

    const std::string reason =
        WriteThemeBundleDirectory(SampleBundle(), dir.Path(), false);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("--force"), std::string::npos);

    EXPECT_EQ(WriteThemeBundleDirectory(SampleBundle(), dir.Path(), true), "");
}

TEST(ThemeBundleDirectoryTest, CreatesTheDirectoryWhenItIsNotThereYet) {
    ScratchDir parent("create");
    const std::string nested = (parent.Native() / "nested" / "theme").string();
    EXPECT_EQ(WriteThemeBundleDirectory(SampleBundle(), nested, false), "");
    EXPECT_TRUE(fs::exists(fs::path(nested) / "theme.json"));
}

TEST(ThemeBundleDirectoryTest, RefusesADirectoryWithNoThemeJson) {
    ScratchDir dir("nomanifest");
    std::ofstream(dir.Native() / "logo.png", std::ios::binary) << kPng;

    Json::Value json;
    std::map<std::string, std::string> assets;
    const std::string reason = ReadThemeBundleDirectory(dir.Path(), json, assets);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("theme.json"), std::string::npos);
}

TEST(ThemeBundleDirectoryTest, RefusesAFileWhoseNameIsNotAUsableAssetName) {
    // The same rule the zip reader applies, so neither transport is the soft
    // way in.
    ScratchDir dir("badname");
    ASSERT_EQ(WriteThemeBundleDirectory(SampleBundle(), dir.Path(), false), "");
    std::ofstream(dir.Native() / ".hidden", std::ios::binary) << kPng;

    Json::Value json;
    std::map<std::string, std::string> assets;
    EXPECT_NE(ReadThemeBundleDirectory(dir.Path(), json, assets), "");
}

TEST(ThemeBundleDirectoryTest, IgnoresSubdirectoriesRatherThanFlatteningThem) {
    // Descending would let two files in different folders collide on one asset
    // name, silently losing one.
    ScratchDir dir("subdir");
    ASSERT_EQ(WriteThemeBundleDirectory(SampleBundle(), dir.Path(), false), "");
    std::error_code ec;
    fs::create_directories(dir.Native() / "extra", ec);
    std::ofstream(dir.Native() / "extra" / "logo.png", std::ios::binary) << "other";

    Json::Value json;
    std::map<std::string, std::string> assets;
    ASSERT_EQ(ReadThemeBundleDirectory(dir.Path(), json, assets), "");
    EXPECT_EQ(assets.size(), 1u);
    EXPECT_EQ(assets["logo.png"], kPng);
}

TEST(ThemeBundleDirectoryTest, RefusesAPathThatIsNotADirectory) {
    ScratchDir dir("notadir");
    const std::string file = (dir.Native() / "theme.json").string();
    std::ofstream(file) << "{}";

    Json::Value json;
    std::map<std::string, std::string> assets;
    EXPECT_NE(ReadThemeBundleDirectory(file, json, assets), "");
}

}  // namespace
}  // namespace Branding
