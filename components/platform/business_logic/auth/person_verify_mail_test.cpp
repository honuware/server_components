#include "person_verify_mail.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "util/mail/tenant_branding.h"
#include "util/types.h"

namespace Auth {
namespace Mail {
namespace {

// Common inputs. Phase 3.3 of the security review: the verification
// link points at the SPA (branding.websiteUrl is the SPA base URL here, not an
// API URL) and uses a query-string `?email=…&secret=…` shape. The
// kApiPrefix value is silently unused by the template but kept in the
// signature for compat.
//
// Phase 1.3 componentization: the studio name is supplied via TenantBranding
// (not hardcoded), so the tests use a deliberately non-Knotty-Yoga studio name
// to prove the substitution works and that no brand literal is baked into the
// template.
constexpr std::string_view kEmailFrom = "user@example.com";
constexpr std::string_view kEncodedSecret = "ABC123";
constexpr std::string_view kFirstName = "First";
constexpr std::string_view kLastName = "Last";
constexpr std::string_view kApiPrefix = "/api/";
constexpr std::string_view kActivation = "verify";
constexpr std::string_view kStudioName = "Test Studio";
constexpr std::string_view kWebsite = "https://example.com/";

::Mail::TenantBranding MakeBranding() {
    ::Mail::TenantBranding branding;
    branding.studioName = std::string(kStudioName);
    branding.senderName = std::string(kStudioName);
    branding.senderAddress = "sender@example.com";
    branding.websiteUrl = std::string(kWebsite);
    return branding;
}

// Expected contents derived from person_verify_mail.cpp templates, with the
// studio name substituted from TenantBranding.
constexpr std::string_view kExpectedPrefix =
R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Email Verification</title>
</head>
<body>
    <h1>Test Studio Account Activation</h1>
    <p>Greetings First,</p>
    <p/>
    <p><a href="https://example.com/verify?email=user@example.com&secret=)";
constexpr std::string_view kExpectedSuffix =
R"(">Click here to activate your account.</a></p>
</body>
</html>
)";
constexpr std::string_view kExpectedFull =
R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Email Verification</title>
</head>
<body>
    <h1>Test Studio Account Activation</h1>
    <p>Greetings First,</p>
    <p/>
    <p><a href="https://example.com/verify?email=user@example.com&secret=ABC123">Click here to activate your account.</a></p>
</body>
</html>
)";

TEST(PersonVerifyMailTest, GenerateVerifyMailBodyPrefixBasic) {
    const std::string prefix = GenerateVerifyMailBodyPrefix(
        kEmailFrom, kFirstName, kLastName, MakeBranding(), kApiPrefix,
        kActivation);
    EXPECT_EQ(prefix, NormalizeCrLf(kExpectedPrefix));
}

TEST(PersonVerifyMailTest, GenerateVerifyMailBodySuffixBasic) {
    const std::string suffix = GenerateVerifyMailBodySuffix(
        kEmailFrom, kFirstName, kLastName, MakeBranding(), kApiPrefix,
        kActivation);
    EXPECT_EQ(suffix, NormalizeCrLf(kExpectedSuffix));
}

TEST(PersonVerifyMailTest, GenerateVerifyMailBodyBasic) {
    const std::string body = GenerateVerifyMailBody(
        kEmailFrom, kEncodedSecret, kFirstName, kLastName, MakeBranding(),
        kApiPrefix, kActivation);
    EXPECT_EQ(body, NormalizeCrLf(kExpectedFull));
}

// Phase 1.3 componentization: the studio name in the <h1> is substituted from
// TenantBranding — a different branding produces different output.
TEST(PersonVerifyMailTest, StudioNameIsSubstitutedFromBranding) {
    ::Mail::TenantBranding branding = MakeBranding();
    branding.studioName = "Another Studio";
    const std::string body = GenerateVerifyMailBody(
        kEmailFrom, kEncodedSecret, kFirstName, kLastName, branding,
        kApiPrefix, kActivation);
    EXPECT_THAT(body,
                testing::HasSubstr("<h1>Another Studio Account Activation</h1>"));
    EXPECT_THAT(body,
                testing::Not(testing::HasSubstr("Test Studio")));
}

// The framework template must not bake in any brand literal — the whole point
// of parameterizing it for the public honuware component.
TEST(PersonVerifyMailTest, NoHardcodedBrandLiteral) {
    const std::string body = GenerateVerifyMailBody(
        kEmailFrom, kEncodedSecret, kFirstName, kLastName, MakeBranding(),
        kApiPrefix, kActivation);
    EXPECT_THAT(body, testing::Not(testing::HasSubstr("Knotty Yoga")));
    EXPECT_THAT(body, testing::Not(testing::HasSubstr("knottyyoga")));
}

// The website URL is likewise supplied via branding, not hardcoded.
TEST(PersonVerifyMailTest, WebsiteUrlIsSubstitutedFromBranding) {
    ::Mail::TenantBranding branding = MakeBranding();
    branding.websiteUrl = "https://other.example.net/";
    const std::string prefix = GenerateVerifyMailBodyPrefix(
        kEmailFrom, kFirstName, kLastName, branding, kApiPrefix, kActivation);
    EXPECT_THAT(prefix,
                testing::HasSubstr("https://other.example.net/verify"));
}

}  // namespace
}  // namespace Mail
}  // namespace Auth
