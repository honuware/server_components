#include "business_logic/scheduled_jobs/admin_alerts_digest.h"

#include <algorithm>
#include <stdexcept>

#include "db_schema/admin_alerts.h"
#include "sql_util/table_helpers/admin_alerts.h"
#include "util/mail/tenant_branding.h"
#include "util/secrets/secret_keys.h"
#include "util/types.h"

namespace PlatformJobs {

namespace {

constexpr std::string_view kDigestLineTemplate = "  [{created_at}] {description}\n";

constexpr std::string_view kDigestBodyTemplate =
R"BODY({site_name} admin alerts

These alerts were recorded since the last digest:

{lines}
Total: {count}
)BODY";

std::string Column(const KeyValueTable& row, std::string_view column) {
    auto it = row.find(std::string(column));
    return it == row.end() ? std::string() : it->second;
}

// A missing, malformed or negative stamp reads as 0: send everything.
int64_t LookupLastNotifiedUs(Transaction& transaction, Secrets::SecretsHelper& secrets) {
    const std::string value = secrets.LookupSecret(transaction, Secrets::kAdminAlertsLastNotifiedUs);
    try {
        return value.empty() ? 0 : std::max<int64_t>(std::stoll(value), 0);
    }
    catch (const std::exception&) {
        return 0;
    }
}

}  // namespace

std::string RenderAdminAlertsDigestBody(std::string_view siteName, const KeyValueTableArray& alerts) {
    std::string lines;
    for (const KeyValueTable& alert : alerts) {
        lines += FormatString(kDigestLineTemplate, {
            { "created_at", Column(alert, DbSchema::kAdminAlertsCreatedAt) },
            { "description", Column(alert, DbSchema::kAdminAlertsDescription) },
        });
    }
    return NormalizeCrLf(FormatString(kDigestBodyTemplate, {
        { "site_name", std::string(siteName) },
        { "lines", lines },
        { "count", StringFromInt(static_cast<int64_t>(alerts.size())) },
    }));
}

int64_t NewestAlertCreatedAt(const KeyValueTableArray& alerts) {
    int64_t newest = 0;
    for (const KeyValueTable& alert : alerts) {
        try {
            newest = std::max<int64_t>(newest, std::stoll(Column(alert, DbSchema::kAdminAlertsCreatedAt)));
        }
        catch (const std::exception&) {
            // Skip a malformed row rather than never moving the stamp, which
            // would resend the same digest every run.
        }
    }
    return newest;
}

AdminAlertsDigestResult SendAdminAlertsDigest(
    Transaction& transaction,
    DatabaseHelper databaseHelper,
    Secrets::SecretsHelper& secrets,
    const ::Mail::MailHelperPtr& mailHelper) {
    AdminAlertsDigestResult result;
    result.lastNotifiedUs = LookupLastNotifiedUs(transaction, secrets);
    const KeyValueTableArray alerts =
        TableHelpers::AdminAlerts(databaseHelper).GetAdminAlertsSince(transaction, result.lastNotifiedUs);
    result.alertsCount = static_cast<int64_t>(alerts.size());
    if (alerts.empty()) {
        return result;
    }
    const std::string recipient = secrets.LookupSecret(transaction, Secrets::kAdminAlertsRecipient);
    if (recipient.empty()) {
        return result;
    }
    if (!mailHelper) {
        throw std::runtime_error("SendAdminAlertsDigest: mail helper unavailable");
    }

    const std::string siteName = ::Mail::LoadTenantBranding(secrets, transaction).studioName;
    std::string subject = secrets.LookupSecret(transaction, Secrets::kAdminAlertsDigestSubject);
    if (subject.empty()) {
        subject = FormatString(kAdminAlertsDefaultSubject, { { "site_name", siteName } });
    }
    ::Mail::MailMessage message(::Mail::LoadSenderAddress(secrets, transaction),
                                ::Mail::MailAddress{ "Admin", recipient });
    message.SetSubject(subject);
    message.SetBodyText(RenderAdminAlertsDigestBody(siteName, alerts));
    mailHelper->SendMail(message);
    result.mailSent = true;

    // GetAdminAlertsSince is strict (created_at > the stamp), so this only
    // moves forward and no alert is sent twice.
    const int64_t newest = NewestAlertCreatedAt(alerts);
    if (newest > result.lastNotifiedUs) {
        secrets.AddSecret(transaction, Secrets::kAdminAlertsLastNotifiedUs, StringFromInt(newest));
        result.lastNotifiedUs = newest;
    }
    return result;
}

}  // namespace PlatformJobs
