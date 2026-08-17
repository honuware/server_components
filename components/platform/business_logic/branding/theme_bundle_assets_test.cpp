#include "business_logic/branding/theme_bundle_assets.h"

#include <string>

#include <gtest/gtest.h>

namespace Branding {
namespace {

// The asset-name rule is what makes importing an untrusted bundle safe: there
// is no path to traverse because a name is never a path. These tests are the
// security boundary, not style checks.

TEST(ThemeBundleAssetsTest, AcceptsOrdinaryFileNames) {
    EXPECT_TRUE(IsValidBundleAssetName("logo.png"));
    EXPECT_TRUE(IsValidBundleAssetName("hero.jpg"));
    EXPECT_TRUE(IsValidBundleAssetName("StudioSans-700-italic.woff2"));
    EXPECT_TRUE(IsValidBundleAssetName("home-1-hero.jpg"));
    EXPECT_TRUE(IsValidBundleAssetName("a"));
    EXPECT_TRUE(IsValidBundleAssetName("theme.json"));
}

TEST(ThemeBundleAssetsTest, RefusesAnythingThatIsAPath) {
    // Each of these is a real attempt someone has made on a real importer.
    EXPECT_FALSE(IsValidBundleAssetName("../evil.png"));
    EXPECT_FALSE(IsValidBundleAssetName("../../etc/passwd"));
    EXPECT_FALSE(IsValidBundleAssetName("sub/dir.png"));
    EXPECT_FALSE(IsValidBundleAssetName("sub\\dir.png"));
    EXPECT_FALSE(IsValidBundleAssetName("/etc/passwd"));
    EXPECT_FALSE(IsValidBundleAssetName("C:\\windows\\win.ini"));
    EXPECT_FALSE(IsValidBundleAssetName("a..b.png"));
}

TEST(ThemeBundleAssetsTest, RefusesDotFilesFlagsAndEmpties) {
    EXPECT_FALSE(IsValidBundleAssetName(""));
    EXPECT_FALSE(IsValidBundleAssetName("."));
    EXPECT_FALSE(IsValidBundleAssetName(".."));
    EXPECT_FALSE(IsValidBundleAssetName(".hidden"));
    // A name that would be read as a command-line flag by anything downstream.
    EXPECT_FALSE(IsValidBundleAssetName("-rf.png"));
    EXPECT_FALSE(IsValidBundleAssetName("_private.png"));
}

TEST(ThemeBundleAssetsTest, RefusesNamesThatAreTooLongOrOddlyEncoded) {
    EXPECT_TRUE(IsValidBundleAssetName(std::string(kMaxAssetNameBytes, 'a')));
    EXPECT_FALSE(IsValidBundleAssetName(std::string(kMaxAssetNameBytes + 1, 'a')));
    EXPECT_FALSE(IsValidBundleAssetName("logo .png"));      // space
    EXPECT_FALSE(IsValidBundleAssetName("logo\n.png"));     // newline
    EXPECT_FALSE(IsValidBundleAssetName("caf\xc3\xa9.png")); // non-ASCII
    // An embedded NUL is what a hostile zip entry name looks like when it is
    // trying to be two different strings at once. Built with an explicit length
    // because a string literal would simply truncate at the NUL and prove
    // nothing.
    EXPECT_FALSE(IsValidBundleAssetName(std::string("logo\0.png", 9)));
}

TEST(ThemeBundleAssetsTest, TellsAUrlApartFromABundledFile) {
    // The `://` test is what lets one string field hold either form.
    EXPECT_TRUE(IsExternalUrlValue("https://cdn.example.com/logo.png"));
    EXPECT_TRUE(IsExternalUrlValue("http://example.com/x"));
    EXPECT_FALSE(IsExternalUrlValue("logo.png"));

    EXPECT_TRUE(IsBundleAssetReference("logo.png"));
    EXPECT_FALSE(IsBundleAssetReference("https://cdn.example.com/logo.png"));
    EXPECT_FALSE(IsBundleAssetReference(""));
    // A URL-shaped value that is ALSO not a valid name must not slip through
    // as a file reference.
    EXPECT_FALSE(IsBundleAssetReference("../evil.png"));
}

TEST(ThemeBundleAssetsTest, CatchesNamesThatCollideOnACaseInsensitiveDisk) {
    // macOS and Windows would silently collapse these into one file, losing an
    // asset. Better to refuse at the source than to export a broken bundle.
    EXPECT_EQ(FindCaseInsensitiveDuplicate({"logo.png", "hero.jpg"}), "");
    EXPECT_EQ(FindCaseInsensitiveDuplicate({"logo.png", "Logo.png"}), "Logo.png");
    EXPECT_EQ(FindCaseInsensitiveDuplicate({"a.PNG", "a.png"}), "a.png");
}

TEST(ThemeBundleAssetsTest, MapsStoredTypesToExtensions) {
    EXPECT_EQ(ExtensionForImageType("jpeg"), "jpg");
    EXPECT_EQ(ExtensionForImageType("image/jpeg"), "jpg");
    EXPECT_EQ(ExtensionForImageType("PNG"), "png");
    EXPECT_EQ(ExtensionForImageType("svg+xml"), "svg");
    EXPECT_EQ(ExtensionForImageType("exe"), "");

    EXPECT_EQ(ExtensionForFontFormat("woff2"), "woff2");
    EXPECT_EQ(ExtensionForFontFormat("OTF"), "otf");
    EXPECT_EQ(ExtensionForFontFormat("html"), "");
}

TEST(ThemeBundleAssetsTest, BuildsDeterministicNamesFromMessyStems) {
    // Deterministic because a round-trip has to be byte-identical: the same
    // photo must produce the same filename on every export.
    EXPECT_EQ(MakeAssetName("Sunrise Studio", "png"), "sunrise-studio.png");
    EXPECT_EQ(MakeAssetName("Sunrise  Studio!!", "png"), "sunrise-studio.png");
    EXPECT_EQ(MakeAssetName("  ", "png"), "asset.png");
    EXPECT_EQ(MakeAssetName("logo", ""), "");

    // A very long stem is truncated to something still valid.
    const std::string name = MakeAssetName(std::string(200, 'x'), "woff2");
    EXPECT_TRUE(IsValidBundleAssetName(name));
    EXPECT_LE(name.size(), kMaxAssetNameBytes);
}

TEST(ThemeBundleAssetsTest, SanitizedStemsNeverProduceAnInvalidName) {
    // Whatever a studio types as its name, the derived filename must pass the
    // rule above — otherwise export produces a bundle import would refuse.
    for (const char* text : {"Knotty Yoga", "café", "../..", "-----", "",
                             "A/B Testing", "Ryan's Studio"}) {
        const std::string name = MakeAssetName(text, "png");
        EXPECT_TRUE(IsValidBundleAssetName(name)) << text << " -> " << name;
    }
}

}  // namespace
}  // namespace Branding
