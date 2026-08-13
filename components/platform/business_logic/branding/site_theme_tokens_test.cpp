#include "business_logic/branding/site_theme_tokens.h"

#include <set>
#include <string>

#include <gtest/gtest.h>

#include "sql_util/database_access/transaction.h"
#include "test/src/util/database_test_helper.h"
#include "util/secrets/secrets_helper_test_util.h"

namespace Branding {
namespace {

const ThemeToken* Find(std::string_view key) {
    for (const ThemeToken& token : SiteThemeTokens()) {
        if (token.key == key) {
            return &token;
        }
    }
    return nullptr;
}

// --- the registry ---

TEST(SiteThemeTokensTest, EveryKeyAndCssVariableIsUnique) {
    // A duplicate key would make one entry unreachable; a duplicate CSS variable
    // would make the applied value depend on map iteration order.
    std::set<std::string> keys;
    std::set<std::string> variables;
    for (const ThemeToken& token : SiteThemeTokens()) {
        EXPECT_TRUE(keys.insert(std::string(token.key)).second)
            << "duplicate key: " << token.key;
        EXPECT_TRUE(variables.insert(std::string(token.cssVariable)).second)
            << "duplicate CSS variable: " << token.cssVariable;
    }
}

TEST(SiteThemeTokensTest, KeysAreSiteThemePrefixedAndVariablesAreCustomProperties) {
    for (const ThemeToken& token : SiteThemeTokens()) {
        EXPECT_EQ(std::string(token.key).rfind("site_theme_", 0), 0u)
            << "theme keys must be site_theme_*: " << token.key;
        EXPECT_EQ(std::string(token.cssVariable).rfind("--", 0), 0u)
            << "must be a CSS custom property: " << token.cssVariable;
    }
}

TEST(SiteThemeTokensTest, CoversTheWholePaletteSoARebrandIsOneLayer) {
    // Overriding a palette step re-brands every role that points at it, which is
    // the cheap re-brand the two-layer scheme exists for. A missing step would
    // leave a studio with one stubbornly Knotty-red hover state.
    for (const char* ramp : {"primary", "secondary", "tertiary", "quaternary",
                             "quinary", "grey"}) {
        for (const char* step : {"100", "200", "300", "400", "500", "600", "700"}) {
            std::string key =
                std::string("site_theme_palette_") + ramp + "_" + step;
            EXPECT_TRUE(Find(key) != nullptr) << "missing palette token: " << key;
        }
    }
}

TEST(SiteThemeTokensTest, PaletteEntriesMapOntoTheStylesheetsVariableNames) {
    const ThemeToken* token = Find("site_theme_palette_primary_400");
    ASSERT_TRUE(token != nullptr);
    EXPECT_EQ(token->cssVariable, "--palette-primary-400");
    EXPECT_EQ(token->type, ThemeTokenType::Color);
}

TEST(SiteThemeTokensTest, CarriesTheRoleRadiusAndTypeTokens) {
    ASSERT_TRUE(Find("site_theme_primary") != nullptr);
    EXPECT_EQ(Find("site_theme_primary")->cssVariable, "--theme-primary");
    EXPECT_EQ(Find("site_theme_inverse_surface")->cssVariable,
              "--theme-inverse-surface");
    EXPECT_EQ(Find("site_theme_radius_card")->cssVariable, "--radius-card");
    EXPECT_EQ(Find("site_theme_radius_card")->type, ThemeTokenType::Length);
    EXPECT_EQ(Find("site_theme_font_body")->cssVariable, "--font-body");
    EXPECT_EQ(Find("site_theme_font_body")->type, ThemeTokenType::FontFamily);
}

// --- per-type validation ---

TEST(SiteThemeTokensTest, ColorTokensTakeHexOnly) {
    EXPECT_TRUE(IsValidThemeTokenValue(ThemeTokenType::Color, "#0B6E4F"));
    EXPECT_FALSE(IsValidThemeTokenValue(ThemeTokenType::Color, "rebeccapurple"));
    EXPECT_FALSE(IsValidThemeTokenValue(ThemeTokenType::Color, "#0B6"));
}

TEST(SiteThemeTokensTest, LengthTokensTakeANumberAndAUnit) {
    EXPECT_TRUE(IsValidThemeTokenValue(ThemeTokenType::Length, "8px"));
    EXPECT_TRUE(IsValidThemeTokenValue(ThemeTokenType::Length, "0.5rem"));
    EXPECT_TRUE(IsValidThemeTokenValue(ThemeTokenType::Length, "0"));
    EXPECT_TRUE(IsValidThemeTokenValue(ThemeTokenType::Length, "9999px"));
    EXPECT_FALSE(IsValidThemeTokenValue(ThemeTokenType::Length, "8"));
    EXPECT_FALSE(IsValidThemeTokenValue(ThemeTokenType::Length, "calc(8px + 1px)"));
    EXPECT_FALSE(IsValidThemeTokenValue(ThemeTokenType::Length, "var(--x)"));
}

TEST(SiteThemeTokensTest, FontFamilyTokensTakeAPlainFamilyList) {
    EXPECT_TRUE(IsValidThemeTokenValue(
        ThemeTokenType::FontFamily, "Roboto, Arial, sans-serif"));
    EXPECT_TRUE(IsValidThemeTokenValue(
        ThemeTokenType::FontFamily, "\"Din Bold\", serif"));
    // The characters that would end the declaration or open a function.
    EXPECT_FALSE(IsValidThemeTokenValue(
        ThemeTokenType::FontFamily, "Roboto; background:url(x)"));
    EXPECT_FALSE(IsValidThemeTokenValue(
        ThemeTokenType::FontFamily, "Roboto, url(evil)"));
    EXPECT_FALSE(IsValidThemeTokenValue(ThemeTokenType::FontFamily, "\"Unbalanced"));
}

TEST(SiteThemeTokensTest, EmptyIsNeverAValidTokenValue) {
    // Clearing a token means deleting the row, not storing "" — an empty custom
    // property is invalid at computed-value time and would break the cascade.
    EXPECT_FALSE(IsValidThemeTokenValue(ThemeTokenType::Color, ""));
    EXPECT_FALSE(IsValidThemeTokenValue(ThemeTokenType::Length, ""));
    EXPECT_FALSE(IsValidThemeTokenValue(ThemeTokenType::FontFamily, ""));
}

// --- LoadSiteTheme ---

TEST(SiteThemeTokensTest, LoadSiteThemeIsEmptyForATenantThatOverrodeNothing) {
    // The normal state: the SPA's stylesheet holds every default, so a tenant
    // with no overrides sends nothing and keeps them all.
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ThemeEmpty", [&](Transaction& transaction) {
        EXPECT_TRUE(LoadSiteTheme(*secrets, transaction).empty());
    });
}

TEST(SiteThemeTokensTest, LoadSiteThemeKeysByCssVariableNotBySecretKey) {
    // The client applies the payload directly onto document.documentElement, so
    // it must arrive keyed by the property name.
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    secrets->AddSecretTest("site_theme_palette_primary_400", "#0B6E4F");
    secrets->AddSecretTest("site_theme_radius_card", "2px");

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ThemeKeys", [&](Transaction& transaction) {
        KeyValueTable theme = LoadSiteTheme(*secrets, transaction);
        EXPECT_EQ(theme.size(), 2u);
        EXPECT_EQ(theme["--palette-primary-400"], "#0B6E4F");
        EXPECT_EQ(theme["--radius-card"], "2px");
        EXPECT_EQ(theme.count("site_theme_radius_card"), 0u);
    });
}

TEST(SiteThemeTokensTest, LoadSiteThemeOmitsJunkRatherThanServingIt) {
    // D10: a hand-edited junk row degrades to the bundled default. Omitting is
    // the right shape here — an empty custom property would break the cascade,
    // so "absent" is the only safe way to say "keep the default".
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    secrets->AddSecretTest("site_theme_primary", "not-a-color");
    secrets->AddSecretTest("site_theme_radius_card", "8");        // no unit
    secrets->AddSecretTest("site_theme_font_body", "X; color:red");
    secrets->AddSecretTest("site_theme_accent", "#123456");        // the good one

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ThemeJunk", [&](Transaction& transaction) {
        KeyValueTable theme = LoadSiteTheme(*secrets, transaction);
        EXPECT_EQ(theme.count("--theme-primary"), 0u);
        EXPECT_EQ(theme.count("--radius-card"), 0u);
        EXPECT_EQ(theme.count("--font-body"), 0u);
        EXPECT_EQ(theme["--theme-accent"], "#123456");
    });
}

TEST(SiteThemeTokensTest, LoadSiteThemeCarriesAFullPaletteRebrand) {
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    for (const char* step : {"100", "200", "300", "400", "500", "600", "700"}) {
        secrets->AddSecretTest(
            std::string("site_theme_palette_primary_") + step, "#0B6E4F");
    }

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ThemeRebrand", [&](Transaction& transaction) {
        KeyValueTable theme = LoadSiteTheme(*secrets, transaction);
        EXPECT_EQ(theme.size(), 7u);
        EXPECT_EQ(theme["--palette-primary-400"], "#0B6E4F");
        EXPECT_EQ(theme["--palette-primary-700"], "#0B6E4F");
    });
}

}  // namespace
}  // namespace Branding
