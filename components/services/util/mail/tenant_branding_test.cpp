#include "tenant_branding.h"

#include <gtest/gtest.h>

#include "mail_helper.h"
#include "test/src/util/database_test_helper.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"

namespace Mail {
namespace {

// Phase 3.2 bootstrap seam: the sender identity + branding come from the
// kMailSenderName / kMailSenderAddress / kWebsiteAddressLogin config secrets, NOT
// from any hardcoded brand. These framework tests set their own secret values and
// assert them back, so they pass in a framework-only build (which registers no app
// brand defaults). The app's specific brand DEFAULTS are verified app-side in
// app_secret_values_test.cpp (AppSecretValuesTest.RegistersBrandDefaultsForFrameworkKeys).

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
    secrets->AddSecretTest(Secrets::kMailSenderName, "Test Studio");
    secrets->AddSecretTest(Secrets::kMailSenderAddress, "sender@studio.test");
    secrets->AddSecretTest(Secrets::kWebsiteAddressLogin, "https://studio.test/login");
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoadTenantBrandingPopulatesAllFields",
        [&](Transaction& transaction) {
            TenantBranding branding = LoadTenantBranding(*secrets, transaction);
            // studioName + senderName both derive from kMailSenderName today.
            EXPECT_EQ(branding.studioName, "Test Studio");
            EXPECT_EQ(branding.senderName, "Test Studio");
            EXPECT_EQ(branding.senderAddress, "sender@studio.test");
            EXPECT_EQ(branding.websiteUrl, "https://studio.test/login");
            EXPECT_FALSE(branding.websiteUrl.empty());
        });
}

}  // namespace
}  // namespace Mail
