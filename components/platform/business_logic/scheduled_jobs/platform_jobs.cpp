#include "business_logic/scheduled_jobs/platform_jobs.h"

#include <stdexcept>
#include <string>

#include "business_logic/auth/token_cleanup_helper.h"
#include "business_logic/images/image_helper.h"
#include "business_logic/scheduled_jobs/admin_alerts_digest.h"
#include "sql_util/table_helpers/idempotency_keys.h"
#include "sql_util/table_helpers/login_attempts.h"
#include "util/secrets/secret_keys.h"

namespace PlatformJobs {

namespace {

int64_t NowUs(Transaction& transaction) {
    return std::stoll(transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));
}

Json::Value CleanupExpiredTokens(Transaction& transaction, DatabaseHelper databaseHelper) {
    Auth::TokenCleanupResult cleanup = Auth::TokenCleanupHelper(databaseHelper).CleanupExpired(transaction);
    return Json::Value(Json::JsonObject{
        { "device_tokens_deleted", Json::Value(cleanup.deviceTokensDeleted) },
        { "email_verifications_deleted", Json::Value(cleanup.emailVerificationsDeleted) },
    });
}

Json::Value CleanupLoginAttempts(Transaction& transaction, DatabaseHelper databaseHelper,
                                 Secrets::SecretsHelper& secrets) {
    const int64_t retentionUs =
        LookupRetentionUs(transaction, secrets, Secrets::kAuthLoginAttemptsRetentionInMicros);
    const int64_t deleted = TableHelpers::LoginAttempts(databaseHelper).PurgeOlderThan(transaction, retentionUs);
    return Json::Value(Json::JsonObject{
        { "login_attempts_deleted", Json::Value(deleted) },
    });
}

Json::Value CleanupScaledPhotos(Transaction& transaction, DatabaseHelper databaseHelper,
                                Secrets::SecretsHelper& secrets) {
    const int64_t maxAgeUs = LookupRetentionUs(transaction, secrets, Secrets::kScaledPhotoMaxAgeUs);
    // Copies regenerate on demand; ImageHelper deletes each one's stored bytes
    // with its row.
    const int64_t deleted =
        Images::ImageHelper(databaseHelper).DeleteScaledPhotosOlderThan(transaction, maxAgeUs);
    return Json::Value(Json::JsonObject{
        { "scaled_photos_deleted", Json::Value(deleted) },
    });
}

Json::Value CleanupIdempotencyKeys(Transaction& transaction, DatabaseHelper databaseHelper) {
    const int64_t deleted =
        TableHelpers::IdempotencyKeys(databaseHelper).DeleteExpiredIdempotencyKeys(transaction, NowUs(transaction));
    return Json::Value(Json::JsonObject{
        { "idempotency_keys_deleted", Json::Value(deleted) },
    });
}

Json::Value NotifyAdminAlerts(Transaction& transaction, DatabaseHelper databaseHelper,
                              Secrets::SecretsHelper& secrets, const ::Mail::MailHelperPtr& mailHelper) {
    AdminAlertsDigestResult digest = SendAdminAlertsDigest(transaction, databaseHelper, secrets, mailHelper);
    return Json::Value(Json::JsonObject{
        { "alerts_count", Json::Value(digest.alertsCount) },
        { "mail_sent", Json::Value(digest.mailSent) },
        { "last_notified_us", Json::Value(digest.lastNotifiedUs) },
    });
}

}  // namespace

const std::vector<std::string_view>& PlatformJobNames() {
    static const std::vector<std::string_view> names = {
        kJobCleanupExpiredTokens,
        kJobCleanupLoginAttempts,
        kJobCleanupScaledPhotos,
        kJobCleanupIdempotencyKeys,
        kJobNotifyAdminAlerts,
    };
    return names;
}

std::optional<Json::Value> RunPlatformJob(
    Transaction& transaction,
    DatabaseHelper databaseHelper,
    Secrets::SecretsHelper& secrets,
    const ::Mail::MailHelperPtr& mailHelper,
    std::string_view jobName) {
    if (jobName == kJobCleanupExpiredTokens) {
        return CleanupExpiredTokens(transaction, databaseHelper);
    }
    if (jobName == kJobCleanupLoginAttempts) {
        return CleanupLoginAttempts(transaction, databaseHelper, secrets);
    }
    if (jobName == kJobCleanupScaledPhotos) {
        return CleanupScaledPhotos(transaction, databaseHelper, secrets);
    }
    if (jobName == kJobCleanupIdempotencyKeys) {
        return CleanupIdempotencyKeys(transaction, databaseHelper);
    }
    if (jobName == kJobNotifyAdminAlerts) {
        return NotifyAdminAlerts(transaction, databaseHelper, secrets, mailHelper);
    }
    return std::nullopt;
}

int64_t LookupRetentionUs(Transaction& transaction, Secrets::SecretsHelper& secrets,
                          std::string_view key, int64_t fallbackUs) {
    const std::string value = secrets.LookupSecret(transaction, key);
    try {
        const int64_t parsed = value.empty() ? 0 : std::stoll(value);
        return parsed > 0 ? parsed : fallbackUs;
    }
    catch (const std::exception&) {
        return fallbackUs;
    }
}

}  // namespace PlatformJobs
