#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"
#include "util/mail/mail_helper.h"
#include "util/secrets/secrets_helper.h"

namespace PlatformJobs {

// H13 — the admin-alerts digest behind the notify_admin_alerts job. It mails
// the admin_alerts rows recorded since the last digest to
// admin_alerts_recipient and stamps admin_alerts_last_notified_us forward to
// the newest one sent. Brand-free: the site's name comes from its mail
// settings.

// The subject when admin_alerts_digest_subject is empty; {site_name} is the
// site's name (mail_sender_name).
inline constexpr std::string_view kAdminAlertsDefaultSubject = "{site_name} admin alerts";

struct AdminAlertsDigestResult {
    int64_t alertsCount = 0;  // new since the last digest, whether sent or not
    bool mailSent = false;
    int64_t lastNotifiedUs = 0;  // the stamp after this run
};

// The plain-text body: a heading with the site's name, one line per alert,
// oldest first, then the total. CRLF line endings, as mailio requires.
std::string RenderAdminAlertsDigestBody(std::string_view siteName, const KeyValueTableArray& alerts);

// The newest created_at among the alerts, or 0 for none.
int64_t NewestAlertCreatedAt(const KeyValueTableArray& alerts);

// Reads the alerts newer than the stamp and:
//   - none: sends nothing and leaves the stamp;
//   - an empty recipient: sends nothing and leaves the stamp, so the alerts
//     wait for a recipient to be configured;
//   - otherwise: one mail from the site's sender, then the stamp moves to the
//     newest alert's created_at.
// A failed send throws (no mail helper counts) before the stamp moves, so the
// next run tries the same alerts again; the endpoint answers 500.
AdminAlertsDigestResult SendAdminAlertsDigest(
    Transaction& transaction,
    DatabaseHelper databaseHelper,
    Secrets::SecretsHelper& secrets,
    const ::Mail::MailHelperPtr& mailHelper);

}  // namespace PlatformJobs
