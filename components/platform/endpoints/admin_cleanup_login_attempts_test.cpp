#include "endpoints/admin_cleanup_login_attempts.h"

#include <string>

#include <gtest/gtest.h>

#include "business_logic/auth/server_config.h"
#include "db_schema/login_attempts.h"
#include "endpoints/scheduled_job_test_support.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/types.h"

// H13 — POST /api/admin/cleanup_login_attempts, over
// LoginAttempts::PurgeOlderThan (tested there).
namespace Endpoints {
namespace {

using namespace ScheduledJobTest;

constexpr int64_t kUsPerHour = 3'600LL * 1'000'000LL;

void AddAttempt(Transaction& transaction, std::string_view email, int64_t attemptedAtUs) {
    transaction.RunSqlStatement(
        "INSERT INTO login_attempts (email_lower, ip, attempted_at, success, kind) "
        "VALUES ($1, $2, $3, $4, $5)",
        std::string(email), std::string("10.0.0.1"), StringFromInt(attemptedAtUs), std::string("f"),
        std::string(DbSchema::kLoginAttemptsKindLogin));
}

int64_t AttemptsFor(Transaction& transaction, std::string_view email) {
    return std::stoll(transaction.RunSqlStatementReturningOneValue(
        "SELECT count(*) FROM login_attempts WHERE email_lower = $1", std::string(email)));
}

// The retention comes from its secret: an hour here, so one attempt is out and
// one is in.
TEST(AdminCleanupLoginAttemptsTest, PurgesPastTheRetentionAndARepeatFindsNothing) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("PurgesPastTheRetention", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        helper.GetSecretsHelper()->AddSecret(transaction, Secrets::kAuthLoginAttemptsRetentionInMicros,
                                             StringFromInt(kUsPerHour));
        const int64_t nowUs = NowUs(transaction);
        AddAttempt(transaction, "old@example.com", nowUs - 2 * kUsPerHour);
        AddAttempt(transaction, "recent@example.com", nowUs - kUsPerHour / 2);
        SignInAsScheduler(transaction, testDb, helper);

        crow::response first = RunJob(helper, "cleanup_login_attempts");
        ASSERT_EQ(first.code, 200) << first.body;
        EXPECT_EQ(Json::Value::FromText(first.body)["login_attempts_deleted"].Get<int64_t>(), 1);
        EXPECT_EQ(AttemptsFor(transaction, "old@example.com"), 0);
        EXPECT_EQ(AttemptsFor(transaction, "recent@example.com"), 1);

        crow::response again = RunJob(helper, "cleanup_login_attempts");
        ASSERT_EQ(again.code, 200) << again.body;
        EXPECT_EQ(Json::Value::FromText(again.body)["login_attempts_deleted"].Get<int64_t>(), 0);
    });
    Auth::ServerConfig::Shutdown();
}

// No secret: 30 days.
TEST(AdminCleanupLoginAttemptsTest, WithoutTheSecretKeepsThirtyDays) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("WithoutTheSecretKeepsThirtyDays", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        const int64_t nowUs = NowUs(transaction);
        AddAttempt(transaction, "old@example.com", nowUs - 31 * 24 * kUsPerHour);
        AddAttempt(transaction, "recent@example.com", nowUs - 29 * 24 * kUsPerHour);
        SignInAsScheduler(transaction, testDb, helper);

        crow::response resp = RunJob(helper, "cleanup_login_attempts");
        ASSERT_EQ(resp.code, 200) << resp.body;
        EXPECT_EQ(Json::Value::FromText(resp.body)["login_attempts_deleted"].Get<int64_t>(), 1);
        EXPECT_EQ(AttemptsFor(transaction, "recent@example.com"), 1);
    });
    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
