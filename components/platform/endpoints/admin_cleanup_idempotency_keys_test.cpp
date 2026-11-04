#include "endpoints/admin_cleanup_idempotency_keys.h"

#include <string>

#include <gtest/gtest.h>

#include "business_logic/auth/server_config.h"
#include "endpoints/scheduled_job_test_support.h"
#include "sql_util/table_helpers/idempotency_keys.h"
#include "util/types.h"

// H13 — POST /api/admin/cleanup_idempotency_keys, over
// IdempotencyKeys::DeleteExpiredIdempotencyKeys (tested there).
namespace Endpoints {
namespace {

using namespace ScheduledJobTest;

constexpr int64_t kUsPerHour = 3'600LL * 1'000'000LL;

int64_t KeyCount(Transaction& transaction, int64_t keyId) {
    return std::stoll(transaction.RunSqlStatementReturningOneValue(
        "SELECT count(*) FROM idempotency_keys WHERE id = $1", StringFromInt(keyId)));
}

TEST(AdminCleanupIdempotencyKeysTest, DeletesExpiredKeysAndARepeatFindsNothing) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeletesExpiredKeys", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        TableHelpers::IdempotencyKeys keys(testDb.GetDatabaseHelper());
        const int64_t nowUs = NowUs(transaction);
        const int64_t expired = keys.AddIdempotencyKey(
            transaction, "user:1", "expired", "/api/sample", "", "completed", "", nowUs - kUsPerHour);
        const int64_t live = keys.AddIdempotencyKey(
            transaction, "user:1", "live", "/api/sample", "", "completed", "", nowUs + kUsPerHour);
        SignInAsScheduler(transaction, testDb, helper);

        crow::response first = RunJob(helper, "cleanup_idempotency_keys");
        ASSERT_EQ(first.code, 200) << first.body;
        EXPECT_EQ(Json::Value::FromText(first.body)["idempotency_keys_deleted"].Get<int64_t>(), 1);
        EXPECT_EQ(KeyCount(transaction, expired), 0);
        EXPECT_EQ(KeyCount(transaction, live), 1);

        crow::response again = RunJob(helper, "cleanup_idempotency_keys");
        ASSERT_EQ(again.code, 200) << again.body;
        EXPECT_EQ(Json::Value::FromText(again.body)["idempotency_keys_deleted"].Get<int64_t>(), 0);
    });
    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
