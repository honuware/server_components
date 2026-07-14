#include "business_logic/auth/quick_account_welcome_mail.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "util/mail/tenant_branding.h"

namespace Auth {
namespace Mail {
namespace {

// Phase 1.3 componentization: the studio name is supplied via TenantBranding
// (not hardcoded), so the tests use a deliberately non-Knotty-Yoga studio name
// to prove the substitution works and that no brand literal is baked into the
// template.
::Mail::TenantBranding MakeBranding() {
    ::Mail::TenantBranding branding;
    branding.studioName = "Test Studio";
    branding.senderName = "Test Studio";
    branding.senderAddress = "sender@example.com";
    branding.websiteUrl = "https://example.com/";
    return branding;
}

TEST(QuickAccountWelcomeMailTest, GenerateBodyContainsFirstName) {
    QuickAccountWelcomeData data;
    data.firstName = "Jane";
    data.email = "jane@example.com";
    data.temporaryPassword = "TempPass123!";

    std::string body = GenerateQuickAccountWelcomeBody(data, MakeBranding());

    EXPECT_TRUE(body.find("Jane") != std::string::npos);
}

TEST(QuickAccountWelcomeMailTest, GenerateBodyContainsEmail) {
    QuickAccountWelcomeData data;
    data.firstName = "Jane";
    data.email = "jane@example.com";
    data.temporaryPassword = "TempPass123!";

    std::string body = GenerateQuickAccountWelcomeBody(data, MakeBranding());

    EXPECT_TRUE(body.find("jane@example.com") != std::string::npos);
}

TEST(QuickAccountWelcomeMailTest, GenerateBodyContainsTemporaryPassword) {
    QuickAccountWelcomeData data;
    data.firstName = "Jane";
    data.email = "jane@example.com";
    data.temporaryPassword = "xYz789!Ab";

    std::string body = GenerateQuickAccountWelcomeBody(data, MakeBranding());

    EXPECT_TRUE(body.find("xYz789!Ab") != std::string::npos);
}

TEST(QuickAccountWelcomeMailTest, GenerateBodyContainsChangePasswordWarning) {
    QuickAccountWelcomeData data;
    data.firstName = "Jane";
    data.email = "jane@example.com";
    data.temporaryPassword = "TempPass123!";

    std::string body = GenerateQuickAccountWelcomeBody(data, MakeBranding());

    EXPECT_TRUE(body.find("change your password") != std::string::npos);
}

TEST(QuickAccountWelcomeMailTest, GenerateBodyHasCrLfLineEndings) {
    QuickAccountWelcomeData data;
    data.firstName = "Jane";
    data.email = "jane@example.com";
    data.temporaryPassword = "TempPass123!";

    std::string body = GenerateQuickAccountWelcomeBody(data, MakeBranding());

    EXPECT_TRUE(body.find("\r\n") != std::string::npos);
    for (size_t i = 0; i < body.size(); ++i) {
        if (body[i] == '\n' && i > 0) {
            EXPECT_EQ(body[i - 1], '\r');
        }
    }
}

// Phase 1.3 componentization: the studio name in the template is substituted
// from TenantBranding, not hardcoded.
TEST(QuickAccountWelcomeMailTest, StudioNameIsSubstitutedFromBranding) {
    QuickAccountWelcomeData data;
    data.firstName = "Jane";
    data.email = "jane@example.com";
    data.temporaryPassword = "TempPass123!";

    ::Mail::TenantBranding branding = MakeBranding();
    branding.studioName = "Another Studio";
    std::string body = GenerateQuickAccountWelcomeBody(data, branding);

    EXPECT_THAT(body, testing::HasSubstr("Welcome to Another Studio"));
    EXPECT_THAT(body, testing::HasSubstr("<h1>Another Studio</h1>"));
    EXPECT_THAT(body, testing::Not(testing::HasSubstr("Test Studio")));
}

// The framework template must not bake in any brand literal.
TEST(QuickAccountWelcomeMailTest, NoHardcodedBrandLiteral) {
    QuickAccountWelcomeData data;
    data.firstName = "Jane";
    data.email = "jane@example.com";
    data.temporaryPassword = "TempPass123!";

    std::string body = GenerateQuickAccountWelcomeBody(data, MakeBranding());

    EXPECT_THAT(body, testing::Not(testing::HasSubstr("Knotty Yoga")));
    EXPECT_THAT(body, testing::Not(testing::HasSubstr("knottyyoga")));
}

}  // namespace
}  // namespace Mail
}  // namespace Auth
