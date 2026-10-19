#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "util/mail/tenant_branding.h"

namespace Auth {
namespace Mail {

// Finishing website Phase 11.3: the "reset your password" mail. Brand-free like
// the other framework mails: the studio name and the site address come from
// the app-supplied TenantBranding.
struct PasswordResetMailData {
    std::string firstName;
    // The account's address, as stored. Not encoded; the link builder does that.
    std::string email;
    // The URL-safe base64 secret. Not encoded either.
    std::string secret;
    // The SPA route the link opens, appended to branding.websiteUrl
    // (Secrets::kWebsitePasswordResetLink, normally "reset-password").
    std::string resetPath;
    // How long the link works, quoted in the body.
    int64_t ttlMinutes = 60;
};

// "{websiteUrl}{resetPath}?email=…&secret=…", each value percent-encoded for a
// query string. Every byte outside A–Z a–z 0–9 - . _ ~ is encoded, so a
// plus-addressed email (a+b@x.com) survives: the SPA's router reads an
// unencoded "+" in a query as a space.
std::string BuildPasswordResetLink(
    std::string_view websiteUrl,
    std::string_view resetPath,
    std::string_view email,
    std::string_view secret);

// "Reset your {studio name} password".
std::string GeneratePasswordResetMailSubject(
    const ::Mail::TenantBranding& branding);

std::string GeneratePasswordResetMailBody(
    const PasswordResetMailData& data,
    const ::Mail::TenantBranding& branding);

}  // namespace Mail
}  // namespace Auth
