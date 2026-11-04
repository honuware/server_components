#include "endpoints/admin_cleanup_expired_tokens.h"

#include <string>

#include <gtest/gtest.h>

#include "business_logic/auth/server_config.h"
#include "endpoints/scheduled_job_test_support.h"
#include "sql_util/table_helpers/device_tokens.h"
#include "sql_util/table_helpers/email_verifications.h"
#include "sql_util/table_helpers/people.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/types.h"

// H13 — POST /api/admin/cleanup_expired_tokens, over TokenCleanupHelper
// (tested there).
namespace Endpoints {
namespace {

using namespace ScheduledJobTest;

constexpr int64_t kUsPerHour = 3'600LL * 1'000'000LL;

int64_t CountRows(Transaction& transaction, std::string_view table, int64_t id) {
    return std::stoll(transaction.RunSqlStatementReturningOneValue(
        "SELECT count(*) FROM " + std::string(table) + " WHERE id = $1", StringFromInt(id)));
}

TEST(AdminCleanupExpiredTokensTest, DeletesExpiredTokensAndARepeatFindsNothing) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeletesExpiredTokens", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        SignInAsScheduler(transaction, testDb, helper);

        const int64_t personId = TableHelpers::People(databaseHelper)
            .AddPerson(transaction, "member@example.com", "Sam", "Sample", "hash");
        TableHelpers::DeviceTokens deviceTokens(databaseHelper);
        const int64_t freshToken = deviceTokens.AddDeviceToken(transaction, personId, "fresh", kUsPerHour);
        const int64_t staleToken = deviceTokens.AddDeviceToken(transaction, personId, "stale", kUsPerHour);
        transaction.RunSqlStatement("UPDATE device_tokens SET expires_at = 1 WHERE id = $1",
                                    StringFromInt(staleToken));
        TableHelpers::EmailVerifications verifications(databaseHelper, helper.GetSecretsHelper());
        const int64_t staleVerification = verifications.AddEmailVerificationById(transaction, personId, "stale");
        transaction.RunSqlStatement("UPDATE email_verifications SET expires_at = 1 WHERE id = $1",
                                    StringFromInt(staleVerification));

        crow::response first = RunJob(helper, "cleanup_expired_tokens");
        ASSERT_EQ(first.code, 200) << first.body;
        Json::Value body = Json::Value::FromText(first.body);
        EXPECT_EQ(body["device_tokens_deleted"].Get<int64_t>(), 1);
        EXPECT_EQ(body["email_verifications_deleted"].Get<int64_t>(), 1);
        EXPECT_EQ(CountRows(transaction, "device_tokens", freshToken), 1);
        EXPECT_EQ(CountRows(transaction, "device_tokens", staleToken), 0);
        EXPECT_EQ(CountRows(transaction, "email_verifications", staleVerification), 0);

        // The scheduler's own session survived, so it can call again.
        crow::response again = RunJob(helper, "cleanup_expired_tokens");
        ASSERT_EQ(again.code, 200) << again.body;
        body = Json::Value::FromText(again.body);
        EXPECT_EQ(body["device_tokens_deleted"].Get<int64_t>(), 0);
        EXPECT_EQ(body["email_verifications_deleted"].Get<int64_t>(), 0);
    });
    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
