#include "password_reset_mail.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "util/mail/tenant_branding.h"
#include "util/types.h"

namespace Auth {
namespace Mail {
namespace {

using ::testing::HasSubstr;
using ::testing::Not;

// A deliberately non-Knotty-Yoga studio, as person_verify_mail_test does, to
// prove the branding is substituted rather than baked in.
::Mail::TenantBranding MakeBranding() {
    ::Mail::TenantBranding branding;
    branding.studioName = "Test Studio";
    branding.senderName = "Test Studio";
    branding.senderAddress = "sender@example.com";
    branding.websiteUrl = "https://example.com/";
    return branding;
}

PasswordResetMailData MakeData() {
    PasswordResetMailData data;
    data.firstName = "Alice";
    data.email = "alice@example.com";
    data.secret = "AbC-123_xyz";
    data.resetPath = "reset-password";
    data.ttlMinutes = 60;
    return data;
}

constexpr std::string_view kExpectedBody =
R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Reset your Test Studio password</title>
</head>
<body>
    <h1>Test Studio</h1>
    <p>Hello Alice,</p>
    <p>Someone asked to reset the password for the Test Studio account alice@example.com.</p>
    <p><a href="https://example.com/reset-password?email=alice%40example.com&secret=AbC-123_xyz">Reset your password</a></p>
    <p>This link expires in 60 minutes and works once.</p>
    <p>If you didn't ask for this, ignore this email. Your password has not changed.</p>
</body>
</html>
)";

TEST(PasswordResetMailTest, BodyMatchesTheTemplate) {
    EXPECT_EQ(GeneratePasswordResetMailBody(MakeData(), MakeBranding()),
              NormalizeCrLf(kExpectedBody));
}

TEST(PasswordResetMailTest, BodyUsesCrLfLineEndings) {
    // mailio rejects a bare \n.
    const std::string body = GeneratePasswordResetMailBody(MakeData(), MakeBranding());
    for (size_t i = 0; i < body.size(); ++i) {
        if (body[i] == '\n') {
            ASSERT_GT(i, 0u);
            EXPECT_EQ(body[i - 1], '\r') << "bare \\n at offset " << i;
        }
    }
}

TEST(PasswordResetMailTest, SubjectNamesTheStudio) {
    EXPECT_EQ(GeneratePasswordResetMailSubject(MakeBranding()),
              "Reset your Test Studio password");
}

TEST(PasswordResetMailTest, BodyQuotesTheLinksLifetime) {
    PasswordResetMailData data = MakeData();
    data.ttlMinutes = 15;
    EXPECT_THAT(GeneratePasswordResetMailBody(data, MakeBranding()),
                HasSubstr("expires in 15 minutes"));
}

TEST(PasswordResetMailTest, BrandingAndRouteAreSubstituted) {
    ::Mail::TenantBranding branding = MakeBranding();
    branding.studioName = "Another Studio";
    branding.websiteUrl = "https://other.example.net/";
    PasswordResetMailData data = MakeData();
    data.resetPath = "account/reset";

    const std::string body = GeneratePasswordResetMailBody(data, branding);
    EXPECT_THAT(body, HasSubstr("<h1>Another Studio</h1>"));
    EXPECT_THAT(body, HasSubstr("https://other.example.net/account/reset?email="));
    EXPECT_THAT(body, Not(HasSubstr("Test Studio")));
    EXPECT_THAT(body, Not(HasSubstr("Knotty")));
    EXPECT_THAT(body, Not(HasSubstr("knottyyoga")));
}

TEST(PasswordResetMailTest, LinkEncodesAPlusAddressedEmail) {
    // An unencoded "+" in a query string reads as a space to the SPA's router,
    // so a+b@x.com would arrive as "a b@x.com" and the reset would fail.
    EXPECT_EQ(BuildPasswordResetLink(
                  "https://example.com/", "reset-password",
                  "mason+test@example.com", "AbC-123_xyz"),
              "https://example.com/reset-password"
              "?email=mason%2Btest%40example.com&secret=AbC-123_xyz");
}

TEST(PasswordResetMailTest, LinkEncodesEverythingOutsideTheUnreservedSet) {
    EXPECT_EQ(BuildPasswordResetLink("https://e.com/", "r", "a b&c=d/e?", "s"),
              "https://e.com/r?email=a%20b%26c%3Dd%2Fe%3F&secret=s");
}

}  // namespace
}  // namespace Mail
}  // namespace Auth
