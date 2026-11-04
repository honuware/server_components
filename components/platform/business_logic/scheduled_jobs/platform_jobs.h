#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/transaction.h"
#include "util/json_value.h"
#include "util/mail/mail_helper.h"
#include "util/secrets/secrets_helper.h"

namespace PlatformJobs {

// H13 — the scheduled jobs every site runs over the framework's own tables:
// what each framework /api/admin/<name> endpoint runs inside its
// run_scheduled_jobs gate, and what an app's tooling can run without a
// server. Each runs in the caller's one transaction, decides for itself
// whether there is work (so a repeat is safe), and returns its counts. An app
// adds its own jobs beside these, behind the same permission.

inline constexpr std::string_view kJobCleanupExpiredTokens = "cleanup_expired_tokens";
inline constexpr std::string_view kJobCleanupLoginAttempts = "cleanup_login_attempts";
inline constexpr std::string_view kJobCleanupScaledPhotos = "cleanup_scaled_photos";
inline constexpr std::string_view kJobCleanupIdempotencyKeys = "cleanup_idempotency_keys";
inline constexpr std::string_view kJobNotifyAdminAlerts = "notify_admin_alerts";

// Every job above, in that order.
const std::vector<std::string_view>& PlatformJobNames();

// Runs the named job and returns its counts; nullopt for a name that isn't
// one of the above.
//   cleanup_expired_tokens   {"device_tokens_deleted", "email_verifications_deleted"}
//   cleanup_login_attempts   {"login_attempts_deleted"}
//   cleanup_scaled_photos    {"scaled_photos_deleted"}
//   cleanup_idempotency_keys {"idempotency_keys_deleted"}
//   notify_admin_alerts      {"alerts_count", "mail_sent", "last_notified_us"}
// Throws when a job fails (only the digest's send can); the caller's
// transaction then rolls back.
std::optional<Json::Value> RunPlatformJob(
    Transaction& transaction,
    DatabaseHelper databaseHelper,
    Secrets::SecretsHelper& secrets,
    const ::Mail::MailHelperPtr& mailHelper,
    std::string_view jobName);

// The cleanups' retention when its secret is missing: the framework seeds
// both auth_login_attempts_retention_in_micros and scaled_photo_max_age_us
// with 30 days.
inline constexpr int64_t kDefaultRetentionUs = 30LL * 24LL * 3'600LL * 1'000'000LL;

// A retention secret in microseconds. Missing, malformed or not positive reads
// as `fallbackUs`, so a database seeded before the secret existed still cleans
// up on a sane schedule.
int64_t LookupRetentionUs(Transaction& transaction, Secrets::SecretsHelper& secrets,
                          std::string_view key, int64_t fallbackUs = kDefaultRetentionUs);

}  // namespace PlatformJobs
