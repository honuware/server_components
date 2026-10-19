#include "password_reset_mail.h"

#include "util/types.h"

namespace Auth {
namespace Mail {

namespace {

constexpr std::string_view kSubjectTemplate = "Reset your {studio_name} password";

constexpr std::string_view kLinkTemplate = "{website_url}{reset_path}?email={email}&secret={secret}";

constexpr std::string_view kBodyTemplate =
R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Reset your {studio_name} password</title>
</head>
<body>
    <h1>{studio_name}</h1>
    <p>Hello {first_name},</p>
    <p>Someone asked to reset the password for the {studio_name} account {email}.</p>
    <p><a href="{reset_link}">Reset your password</a></p>
    <p>This link expires in {ttl_minutes} minutes and works once.</p>
    <p>If you didn't ask for this, ignore this email. Your password has not changed.</p>
</body>
</html>
)HTML";

}  // namespace

std::string BuildPasswordResetLink(
    std::string_view websiteUrl,
    std::string_view resetPath,
    std::string_view email,
    std::string_view secret) {
    KeyValueTable params;
    params["website_url"] = std::string(websiteUrl);
    params["reset_path"] = std::string(resetPath);
    params["email"] = URLEncodeQueryValue(email);
    params["secret"] = URLEncodeQueryValue(secret);
    return FormatString(kLinkTemplate, params);
}

std::string GeneratePasswordResetMailSubject(
    const ::Mail::TenantBranding& branding) {
    KeyValueTable params;
    params["studio_name"] = branding.studioName;
    return FormatString(kSubjectTemplate, params);
}

std::string GeneratePasswordResetMailBody(
    const PasswordResetMailData& data,
    const ::Mail::TenantBranding& branding) {
    KeyValueTable params;
    params["studio_name"] = branding.studioName;
    params["first_name"] = data.firstName;
    params["email"] = data.email;
    params["reset_link"] = BuildPasswordResetLink(
        branding.websiteUrl, data.resetPath, data.email, data.secret);
    params["ttl_minutes"] = std::to_string(data.ttlMinutes);
    return NormalizeCrLf(FormatString(kBodyTemplate, params));
}

}  // namespace Mail
}  // namespace Auth
