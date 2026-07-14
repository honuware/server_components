#include "tenant_branding.h"

#include <gtest/gtest.h>

#include "mail_helper.h"
#include "test/src/util/database_test_helper.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"

namespace Mail {
namespace {

// Phase 3.2 bootstrap seam: the sender identity must come from the
// kMailSenderName / kMailSenderAddress config secrets (default VALUES registered
// app-side in app_secret_values.cpp), NOT from the brand-free key-name constants.
// The in-memory test secrets helper auto-loads the app defaults.

TEST(TenantBrandingTest, LoadSenderAddressReturnsConfiguredSender) {
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoadSenderAddressReturnsConfiguredSender",
        [&](Transaction& transaction) {
            MailAddress sender = LoadSenderAddress(*secrets, transaction);
            EXPECT_EQ(sender.name, "Knotty Yoga and Spa");
            EXPECT_EQ(sender.address, "knottyyogaandspa@gmail.com");
        });
}

// Proves the value is read from the secret store, not baked into the constant:
// overriding the secret changes the loaded sender.
TEST(TenantBrandingTest, LoadSenderAddressReflectsOverriddenSecrets) {
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    secrets->AddSecretTest(Secrets::kMailSenderName, "Acme Movement Studio");
    secrets->AddSecretTest(Secrets::kMailSenderAddress, "hello@acme.test");
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoadSenderAddressReflectsOverriddenSecrets",
        [&](Transaction& transaction) {
            MailAddress sender = LoadSenderAddress(*secrets, transaction);
            EXPECT_EQ(sender.name, "Acme Movement Studio");
            EXPECT_EQ(sender.address, "hello@acme.test");
        });
}

TEST(TenantBrandingTest, LoadTenantBrandingPopulatesAllFields) {
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoadTenantBrandingPopulatesAllFields",
        [&](Transaction& transaction) {
            TenantBranding branding = LoadTenantBranding(*secrets, transaction);
            EXPECT_EQ(branding.studioName, "Knotty Yoga and Spa");
            EXPECT_EQ(branding.senderName, "Knotty Yoga and Spa");
            EXPECT_EQ(branding.senderAddress, "knottyyogaandspa@gmail.com");
            // websiteUrl comes from kWebsiteAddressLogin (dev/prod differ), so
            // assert it matches the configured secret rather than a literal.
            EXPECT_EQ(branding.websiteUrl,
                secrets->LookupSecretTest(Secrets::kWebsiteAddressLogin));
            EXPECT_FALSE(branding.websiteUrl.empty());
        });
}

}  // namespace
}  // namespace Mail
