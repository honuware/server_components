#include "business_logic/scheduled_jobs/platform_jobs.h"

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "test/src/util/database_test_helper.h"
#include "util/mail/mail_helper_test_util.h"

// H13 — the framework's jobs by name, as its endpoints call them. The digest's
// rules are tested in admin_alerts_digest_test, each cleanup's in its endpoint
// test and its helper's own test; this pins the names, the count keys and the
// retention lookup.
namespace PlatformJobs {
namespace {

std::set<std::string> KeysOf(const Json::Value& value) {
    std::set<std::string> keys;
    for (const auto& [key, child] : value.GetChildren()) {
        keys.insert(key);
    }
    return keys;
}

TEST(PlatformJobsTest, EveryJobRunsOnAnEmptyDatabaseAndReportsItsCounts) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EveryJobRunsOnAnEmptyDatabase", [&](Transaction& transaction) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        Secrets::SecretsHelperPtr secrets = Secrets::MakeSecretsHelper(db);
        ::Mail::Test::TestMailHelperPtr mail = ::Mail::Test::MakeTestMailHelper();
        const std::vector<std::pair<std::string_view, std::set<std::string>>> expected = {
            { kJobCleanupExpiredTokens, { "device_tokens_deleted", "email_verifications_deleted" } },
            { kJobCleanupLoginAttempts, { "login_attempts_deleted" } },
            { kJobCleanupScaledPhotos, { "scaled_photos_deleted" } },
            { kJobCleanupIdempotencyKeys, { "idempotency_keys_deleted" } },
            { kJobNotifyAdminAlerts, { "alerts_count", "mail_sent", "last_notified_us" } },
        };
        ASSERT_EQ(PlatformJobNames().size(), expected.size());
        for (size_t index = 0; index < expected.size(); ++index) {
            const auto& [name, keys] = expected[index];
            EXPECT_EQ(PlatformJobNames()[index], name);
            std::optional<Json::Value> counts = RunPlatformJob(transaction, db, *secrets, mail, name);
            ASSERT_TRUE(counts.has_value()) << name;
            EXPECT_EQ(KeysOf(*counts), keys) << name;
        }
        EXPECT_TRUE(mail->GetMessages().empty());
    });
}

// An app's own job name isn't one of these.
TEST(PlatformJobsTest, AnUnknownJobIsNothing) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AnUnknownJobIsNothing", [&](Transaction& transaction) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        Secrets::SecretsHelperPtr secrets = Secrets::MakeSecretsHelper(db);
        EXPECT_FALSE(RunPlatformJob(transaction, db, *secrets, nullptr, "archive_past_events").has_value());
        EXPECT_FALSE(RunPlatformJob(transaction, db, *secrets, nullptr, "").has_value());
    });
}

TEST(PlatformJobsTest, RetentionFallsBackWhenUnsetMalformedOrNotPositive) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RetentionFallsBack", [&](Transaction& transaction) {
        Secrets::SecretsHelperPtr secrets = Secrets::MakeSecretsHelper(testDb.GetDatabaseHelper());
        constexpr std::string_view kKey = "test_retention_us";
        EXPECT_EQ(LookupRetentionUs(transaction, *secrets, kKey), kDefaultRetentionUs);
        EXPECT_EQ(LookupRetentionUs(transaction, *secrets, kKey, 42), 42);

        for (const char* bad : { "soon", "0", "-5" }) {
            secrets->AddSecret(transaction, kKey, bad);
            EXPECT_EQ(LookupRetentionUs(transaction, *secrets, kKey, 42), 42) << bad;
        }
        secrets->AddSecret(transaction, kKey, "3600000000");
        EXPECT_EQ(LookupRetentionUs(transaction, *secrets, kKey, 42), 3600000000LL);
    });
}

}  // namespace
}  // namespace PlatformJobs
