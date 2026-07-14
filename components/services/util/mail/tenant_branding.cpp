#include "util/mail/tenant_branding.h"

#include <string>

#include "sql_util/database_access/transaction.h"
#include "util/mail/mail_helper.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper.h"

namespace Mail {

MailAddress LoadSenderAddress(
    Secrets::SecretsHelper& secrets, Transaction& transaction) {
    return MailAddress{
        secrets.LookupSecret(transaction, Secrets::kMailSenderName),
        secrets.LookupSecret(transaction, Secrets::kMailSenderAddress)};
}

TenantBranding LoadTenantBranding(
    Secrets::SecretsHelper& secrets, Transaction& transaction) {
    std::string senderName =
        secrets.LookupSecret(transaction, Secrets::kMailSenderName);
    TenantBranding branding;
    // The studio name doubles as the From-header display name today; the
    // tenancy project can diverge them per-tenant.
    branding.studioName = senderName;
    branding.senderName = senderName;
    branding.senderAddress =
        secrets.LookupSecret(transaction, Secrets::kMailSenderAddress);
    branding.websiteUrl =
        secrets.LookupSecret(transaction, Secrets::kWebsiteAddressLogin);
    return branding;
}

}  // namespace Mail
