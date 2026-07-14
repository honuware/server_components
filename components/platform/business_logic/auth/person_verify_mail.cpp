#include "person_verify_mail.h"

#include "util/types.h"

namespace {

// Phase 3.3 of the security review: the verify link points at the SPA
// (which then POSTs to /api/verify) using a query-string format
// `?email=…&secret=…`. The previous URL-path form put the secret in
// the URL where it landed in CloudFront / reverse-proxy access logs.
// The {api_link_prefix} placeholder is silently unused here; the
// function signature still takes it for backwards-compat with the test
// util.
//
// Phase 1.3 componentization: the studio name in the <h1> is no longer
// hardcoded — it is substituted from TenantBranding.studioName so this
// framework mail builder can ship to the public honuware repo brand-free.
constexpr std::string_view kMailTemplatePrefix =
        R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Email Verification</title>
</head>
<body>
    <h1>{studio_name} Account Activation</h1>
    <p>Greetings {first_name},</p>
    <p/>
    <p><a href="{website_address}{activation_link}?email={email_from}&secret=)HTML";

constexpr std::string_view kMailTemplateSuffix =
R"HTML(">Click here to activate your account.</a></p>
</body>
</html>
)HTML";

std::string GenerateVerifyMailBodyPrefixHelper(
    const KeyValueTable& keyValueTable) {
    return NormalizeCrLf(FormatString(kMailTemplatePrefix, keyValueTable));
}

std::string GenerateVerifyMailBodySuffixHelper(
    const KeyValueTable& keyValueTable) {
    return NormalizeCrLf(FormatString(kMailTemplateSuffix, keyValueTable));
}

// Populate the placeholders shared by every builder. Branding (studio name +
// website URL) comes from the app-supplied TenantBranding; the rest are the
// per-recipient / routing values.
KeyValueTable BuildKeyValueTable(
    std::string_view emailFrom,
    std::string_view firstNameTo,
    std::string_view lastNameTo,
    const ::Mail::TenantBranding& branding,
    std::string_view apiLinkPrefix,
    std::string_view activationLink) {
    KeyValueTable keyValueTable;
    AddToKeyValueTable(keyValueTable, "email_from", emailFrom);
    AddToKeyValueTable(keyValueTable, "first_name", firstNameTo);
    AddToKeyValueTable(keyValueTable, "last_name", lastNameTo);
    AddToKeyValueTable(keyValueTable, "studio_name", branding.studioName);
    AddToKeyValueTable(keyValueTable, "website_address", branding.websiteUrl);
    AddToKeyValueTable(keyValueTable, "api_link_prefix", apiLinkPrefix);
    AddToKeyValueTable(keyValueTable, "activation_link", activationLink);
    return keyValueTable;
}

}  // namespace {

namespace Auth {
namespace Mail {

std::string GenerateVerifyMailBody(
    std::string_view emailFrom,
    std::string_view encodedSecret,
    std::string_view firstNameTo,
    std::string_view lastNameTo,
    const ::Mail::TenantBranding& branding,
    std::string_view apiLinkPrefix,
    std::string_view activationLink) {
    KeyValueTable keyValueTable = BuildKeyValueTable(
        emailFrom, firstNameTo, lastNameTo, branding, apiLinkPrefix,
        activationLink);
    AddToKeyValueTable(keyValueTable, "encoded_secret", encodedSecret);
    return GenerateVerifyMailBodyPrefixHelper(keyValueTable)
        + std::string(encodedSecret)
        + GenerateVerifyMailBodySuffixHelper(keyValueTable);
}

std::string GenerateVerifyMailBodyPrefix(
    std::string_view emailFrom,
    std::string_view firstNameTo,
    std::string_view lastNameTo,
    const ::Mail::TenantBranding& branding,
    std::string_view apiLinkPrefix,
    std::string_view activationLink) {
    KeyValueTable keyValueTable = BuildKeyValueTable(
        emailFrom, firstNameTo, lastNameTo, branding, apiLinkPrefix,
        activationLink);
    return GenerateVerifyMailBodyPrefixHelper(keyValueTable);
}

std::string GenerateVerifyMailBodySuffix(
    std::string_view emailFrom,
    std::string_view firstNameTo,
    std::string_view lastNameTo,
    const ::Mail::TenantBranding& branding,
    std::string_view apiLinkPrefix,
    std::string_view activationLink) {
    KeyValueTable keyValueTable = BuildKeyValueTable(
        emailFrom, firstNameTo, lastNameTo, branding, apiLinkPrefix,
        activationLink);
    return GenerateVerifyMailBodySuffixHelper(keyValueTable);
}

}  // namespace Mail {
}  // namespace Auth {
