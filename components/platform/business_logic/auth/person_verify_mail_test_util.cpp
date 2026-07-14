#include "person_verify_mail_test_util.h"

#include "util/secrets/secret_keys.h"

namespace Auth {
namespace Mail {
namespace Test {

std::string GetEncodedToken(
    Secrets::Test::SecretsHelperTestPtr secrets,
    std::string_view emailFrom,
    std::string_view firstNameTo,
    std::string_view lastNameTo,
    std::string_view mailBody) {
    auto prefix = GenerateVerifyMailBodyPrefix(
        secrets, emailFrom, firstNameTo, lastNameTo);
    auto suffix = GenerateVerifyMailBodySuffix(
        secrets, emailFrom, firstNameTo, lastNameTo);
    std::string_view encodedToken = mailBody;
    encodedToken.remove_prefix(prefix.size());
    encodedToken.remove_suffix(suffix.size());
    return std::string(encodedToken);
}

namespace {

// Build the same TenantBranding that PersonHelper::PreliminaryRegisterPerson
// assembles at the call site, so the test-computed prefix/suffix match the mail
// body actually sent.
::Mail::TenantBranding BuildBranding(
    Secrets::Test::SecretsHelperTestPtr secrets) {
    ::Mail::TenantBranding branding;
    branding.studioName = secrets->LookupSecretTest(Secrets::kMailSenderName);
    branding.senderName = branding.studioName;
    branding.senderAddress =
        secrets->LookupSecretTest(Secrets::kMailSenderAddress);
    // Phase 3.3 of the security review: verification mail links at the SPA
    // (kWebsiteAddressLogin), not the API.
    branding.websiteUrl =
        secrets->LookupSecretTest(Secrets::kWebsiteAddressLogin);
    return branding;
}

}  // namespace

std::string GenerateVerifyMailBodyPrefix(
    Secrets::Test::SecretsHelperTestPtr secrets,
    std::string_view emailFrom,
    std::string_view firstNameTo,
    std::string_view lastNameTo) {
    return Auth::Mail::GenerateVerifyMailBodyPrefix(
        emailFrom,
        firstNameTo,
        lastNameTo,
        BuildBranding(secrets),
        secrets->LookupSecretTest(Secrets::kWebsiteApiLinkPrefix),
        secrets->LookupSecretTest(Secrets::kWebsiteActivationLink));
}

std::string GenerateVerifyMailBodySuffix(
    Secrets::Test::SecretsHelperTestPtr secrets,
    std::string_view emailFrom,
    std::string_view firstNameTo,
    std::string_view lastNameTo) {
    return Auth::Mail::GenerateVerifyMailBodySuffix(
        emailFrom,
        firstNameTo,
        lastNameTo,
        BuildBranding(secrets),
        secrets->LookupSecretTest(Secrets::kWebsiteApiLinkPrefix),
        secrets->LookupSecretTest(Secrets::kWebsiteActivationLink));
}

}  // namespace Test {
}  // namespace Mail {
}  // namespace Auth {