#pragma once

#include <string>

#include "util/mail/mail_helper.h"     // Mail::MailAddress + Secrets fwd-decls
#include "util/secrets/secret_keys.h"  // Secrets::kMailSenderName/Address keys

class Transaction;

namespace Mail {

// Phase 1.3 componentization / tenancy prerequisite: the brand identity that
// framework mail builders substitute into their templates instead of hardcoding
// "Knotty Yoga". Framework code (util/, business_logic/auth/) must ship to the
// public honuware repo brand-free, so any framework mail body that used to
// contain a literal studio name now reads it from a TenantBranding populated by
// the app.
//
// The values come app-side from the registered secret defaults
// (business_logic/app_secret_values.cpp — kMailSenderName, kMailSenderAddress,
// kWebsiteAddressLogin) and, once the tenancy project lands, per-tenant.
//
// App-side mail builders (payment/, scheduling/) keep their own brand literals
// for now; they are only parameterized in the tenancy project's Phase 4.5.
struct TenantBranding {
    std::string studioName;     // Studio/brand name, e.g. "Knotty Yoga and Spa"
    std::string senderName;     // From-header display name
    std::string senderAddress;  // From-header email address
    std::string websiteUrl;     // SPA base URL used to build links
};

// Phase 3.2 bootstrap seam: load the studio sender identity from the
// kMailSenderName / kMailSenderAddress config secrets (default VALUES registered
// app-side in app_secret_values.cpp). Every mail-sending site builds its From
// address from these, so this is the single lookup point — the key constants are
// brand-free key names, so the value MUST come from SecretsHelper, never from the
// constant itself.
MailAddress LoadSenderAddress(
    Secrets::SecretsHelper& secrets, Transaction& transaction);

// Fuller identity for framework mail builders that take a TenantBranding
// (e.g. quick_account_welcome_mail). studioName/senderName both use
// kMailSenderName today; websiteUrl uses kWebsiteAddressLogin.
TenantBranding LoadTenantBranding(
    Secrets::SecretsHelper& secrets, Transaction& transaction);

}  // namespace Mail
