#include "business_logic/branding/site_content_slots.h"

#include <set>
#include <string>

#include <gtest/gtest.h>

#include "sql_util/database_access/transaction.h"
#include "test/src/util/database_test_helper.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"

// Framework tests: the registry is brand-free, so every assertion here either
// checks the KEY set or sets its own invented values and reads them back. Knotty
// Yoga's default VALUES are asserted app-side in app_secret_values_test.cpp.

namespace Branding {
namespace {

std::set<std::string> SlotKeys() {
    std::set<std::string> keys;
    for (const ContentSlot& slot : SiteContentSlots()) {
        keys.insert(std::string(slot.key));
    }
    return keys;
}

SlotType TypeOf(std::string_view key) {
    for (const ContentSlot& slot : SiteContentSlots()) {
        if (slot.key == key) {
            return slot.type;
        }
    }
    ADD_FAILURE() << "slot not registered: " << key;
    return SlotType::Line;
}

// --- The registry itself ---

TEST(SiteContentSlotsTest, RegistryContainsEveryCatalogedSlot) {
    std::set<std::string> keys = SlotKeys();
    for (std::string_view expected : {
             Secrets::kSiteLogoAlt,
             Secrets::kSiteBrowserTitle,
             Secrets::kSiteFaviconUrl,
             Secrets::kSiteHeroHeadline,
             Secrets::kSiteHeroSubline,
             Secrets::kSiteHeroImageUrl,
             Secrets::kSiteTaglineLines,
             Secrets::kSiteAddressLines,
             Secrets::kSiteContactEmail,
             Secrets::kSiteAboutMarkdown,
             Secrets::kSiteStartIntro,
             Secrets::kSiteMembershipBlurb,
             Secrets::kSiteSocialLinks,
         }) {
        EXPECT_TRUE(keys.count(std::string(expected)))
            << "missing content slot: " << expected;
    }
    EXPECT_EQ(keys.size(), SiteContentSlots().size())
        << "a key is registered twice";
}

TEST(SiteContentSlotsTest, RegistryExcludesTheTopLevelBrandingFields) {
    // display_name / website_url / logo_url already ship as top-level fields of
    // the site_info response; carrying them in `content` too would be the exact
    // duplication the tenancy plan's 7.4 note warns against.
    std::set<std::string> keys = SlotKeys();
    EXPECT_FALSE(keys.count(std::string(Secrets::kMailSenderName)));
    EXPECT_FALSE(keys.count(std::string(Secrets::kWebsiteAddressLogin)));
    EXPECT_FALSE(keys.count(std::string(Secrets::kSiteLogoUrl)));
}

TEST(SiteContentSlotsTest, RegistryExposesNoCredentialBearingSecret) {
    // The registry decides what a PUBLIC, unauthenticated, cached endpoint
    // publishes. Every entry must be a `site_*` copy key — never a credential.
    std::set<std::string> keys = SlotKeys();
    EXPECT_FALSE(keys.count(std::string(Secrets::kMailAppPassword)));
    EXPECT_FALSE(keys.count(std::string(Secrets::kAdminAlertsRecipient)));
    for (const ContentSlot& slot : SiteContentSlots()) {
        EXPECT_EQ(std::string(slot.key).rfind("site_", 0), 0u)
            << "content slots must be site_* keys: " << slot.key;
    }
}

TEST(SiteContentSlotsTest, SlotTypesMatchTheValueEachSlotHolds) {
    EXPECT_EQ(TypeOf(Secrets::kSiteAboutMarkdown), SlotType::Markdown);
    EXPECT_EQ(TypeOf(Secrets::kSiteFaviconUrl), SlotType::Url);
    EXPECT_EQ(TypeOf(Secrets::kSiteHeroImageUrl), SlotType::Url);
    EXPECT_EQ(TypeOf(Secrets::kSiteTaglineLines), SlotType::Lines);
    EXPECT_EQ(TypeOf(Secrets::kSiteAddressLines), SlotType::Lines);
    EXPECT_EQ(TypeOf(Secrets::kSiteSocialLinks), SlotType::Lines);
    EXPECT_EQ(TypeOf(Secrets::kSiteHeroHeadline), SlotType::Line);
    EXPECT_EQ(TypeOf(Secrets::kSiteBrowserTitle), SlotType::Line);
}

// --- LoadSiteContent ---

TEST(SiteContentSlotsTest, LoadSiteContentReturnsEveryRegisteredSlotKey) {
    // Whatever the consuming app seeded (this suite runs both standalone and
    // inside an app's test executable), the payload always carries exactly the
    // registry's key set — no more, no fewer.
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "LoadSiteContentReturnsEverySlot", [&](Transaction& transaction) {
            KeyValueTable content = LoadSiteContent(*secrets, transaction);
            EXPECT_EQ(content.size(), SiteContentSlots().size());
            for (const ContentSlot& slot : SiteContentSlots()) {
                // Present-but-empty, never omitted: the SPA merges non-empty
                // values over its bundled defaults, so a stable shape matters
                // more than a compact payload.
                EXPECT_EQ(content.count(std::string(slot.key)), 1u)
                    << "slot missing from payload: " << slot.key;
            }
        });
}

TEST(SiteContentSlotsTest, LoadSiteContentReturnsTheTenantsStoredValues) {
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    secrets->AddSecretTest(Secrets::kSiteHeroHeadline, "Acme Studio moves people");
    secrets->AddSecretTest(Secrets::kSiteContactEmail, "hello@acme.test");
    secrets->AddSecretTest(Secrets::kSiteFaviconUrl, "https://acme.test/fav.ico");
    secrets->AddSecretTest(
        Secrets::kSiteAboutMarkdown, "# About Acme\n\nWe have been here a while.");
    secrets->AddSecretTest(
        Secrets::kSiteSocialLinks,
        "Facebook|https://facebook.com/acme\nInstagram|https://instagram.com/acme");
    // Explicitly cleared, so the "unset reads back empty" assertion below holds
    // no matter which app's defaults the surrounding test binary registered.
    secrets->AddSecretTest(Secrets::kSiteMembershipBlurb, "");

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "LoadSiteContentStoredValues", [&](Transaction& transaction) {
            KeyValueTable content = LoadSiteContent(*secrets, transaction);
            EXPECT_EQ(content[std::string(Secrets::kSiteHeroHeadline)],
                      "Acme Studio moves people");
            EXPECT_EQ(content[std::string(Secrets::kSiteContactEmail)],
                      "hello@acme.test");
            EXPECT_EQ(content[std::string(Secrets::kSiteFaviconUrl)],
                      "https://acme.test/fav.ico");
            EXPECT_EQ(content[std::string(Secrets::kSiteAboutMarkdown)],
                      "# About Acme\n\nWe have been here a while.");
            EXPECT_EQ(
                content[std::string(Secrets::kSiteSocialLinks)],
                "Facebook|https://facebook.com/acme\n"
                "Instagram|https://instagram.com/acme");
            // A cleared slot still reports as empty rather than disappearing —
            // that is what lets the SPA keep its bundled default for it.
            EXPECT_EQ(content[std::string(Secrets::kSiteMembershipBlurb)], "");
        });
}

TEST(SiteContentSlotsTest, LoadSiteContentBlanksValuesThatFailValidation) {
    // D10: a hand-edited junk row degrades to the SPA's bundled default instead
    // of poisoning every page. Nothing else in the payload is affected.
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    secrets->AddSecretTest(Secrets::kSiteHeroImageUrl, "javascript:alert(1)");
    secrets->AddSecretTest(Secrets::kSiteBrowserTitle, "Acme\nStudio");
    secrets->AddSecretTest(Secrets::kSiteStartIntro, "Start here.");

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "LoadSiteContentBlanksJunk", [&](Transaction& transaction) {
            KeyValueTable content = LoadSiteContent(*secrets, transaction);
            EXPECT_EQ(content[std::string(Secrets::kSiteHeroImageUrl)], "");
            EXPECT_EQ(content[std::string(Secrets::kSiteBrowserTitle)], "");
            EXPECT_EQ(content[std::string(Secrets::kSiteStartIntro)], "Start here.");
        });
}

TEST(SiteContentSlotsTest, LoadSiteContentNormalizesWindowsLineEndings) {
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    secrets->AddSecretTest(
        Secrets::kSiteAddressLines, "1 Example Way\r\nRedmond, WA 98052");

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "LoadSiteContentNormalizesCrLf", [&](Transaction& transaction) {
            KeyValueTable content = LoadSiteContent(*secrets, transaction);
            EXPECT_EQ(content[std::string(Secrets::kSiteAddressLines)],
                      "1 Example Way\nRedmond, WA 98052");
        });
}

}  // namespace
}  // namespace Branding
