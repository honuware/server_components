#include "endpoints/admin_cleanup_scaled_photos.h"

#include <string>

#include <gtest/gtest.h>

#include "business_logic/auth/server_config.h"
#include "endpoints/scheduled_job_test_support.h"
#include "sql_util/table_helpers/photo_instances.h"
#include "sql_util/table_helpers/scaled_photos.h"
#include "sql_util/table_helpers/source_photos.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/types.h"

// H13 — POST /api/admin/cleanup_scaled_photos, over
// ImageHelper::DeleteScaledPhotosOlderThan (tested there).
namespace Endpoints {
namespace {

using namespace ScheduledJobTest;

constexpr int64_t kUsPerHour = 3'600LL * 1'000'000LL;

// A cached scaled copy of a new source photo, last used at `lastUsedUs`.
int64_t AddScaledPhoto(Transaction& transaction, DatabaseHelper databaseHelper, int64_t lastUsedUs) {
    TableHelpers::PhotoInstances photoInstances(databaseHelper);
    const int64_t sourcePhotoId = TableHelpers::SourcePhotos(databaseHelper).AddSourcePhoto(
        transaction, photoInstances.AddPhotoInstance(transaction, "source", "jpeg", 2000, 1500));
    const int64_t scaledPhotoId = TableHelpers::ScaledPhotos(databaseHelper).AddScaledPhoto(
        transaction, sourcePhotoId, photoInstances.AddPhotoInstance(transaction, "scaled", "jpeg", 200, 150),
        200, 150);
    transaction.RunSqlStatement("UPDATE scaled_photos SET last_used_at_us = $1 WHERE scaled_photo_id = $2",
                                StringFromInt(lastUsedUs), StringFromInt(scaledPhotoId));
    return scaledPhotoId;
}

int64_t ScaledPhotoCount(Transaction& transaction, int64_t scaledPhotoId) {
    return std::stoll(transaction.RunSqlStatementReturningOneValue(
        "SELECT count(*) FROM scaled_photos WHERE scaled_photo_id = $1", StringFromInt(scaledPhotoId)));
}

// The maximum age comes from its secret: an hour here, so one copy is out and
// one is in.
TEST(AdminCleanupScaledPhotosTest, DeletesStaleCopiesAndARepeatFindsNothing) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeletesStaleCopies", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        helper.GetSecretsHelper()->AddSecret(transaction, Secrets::kScaledPhotoMaxAgeUs,
                                             StringFromInt(kUsPerHour));
        const int64_t nowUs = NowUs(transaction);
        const int64_t stale = AddScaledPhoto(transaction, databaseHelper, nowUs - 2 * kUsPerHour);
        const int64_t recent = AddScaledPhoto(transaction, databaseHelper, nowUs - kUsPerHour / 2);
        SignInAsScheduler(transaction, testDb, helper);

        crow::response first = RunJob(helper, "cleanup_scaled_photos");
        ASSERT_EQ(first.code, 200) << first.body;
        EXPECT_EQ(Json::Value::FromText(first.body)["scaled_photos_deleted"].Get<int64_t>(), 1);
        EXPECT_EQ(ScaledPhotoCount(transaction, stale), 0);
        EXPECT_EQ(ScaledPhotoCount(transaction, recent), 1);

        crow::response again = RunJob(helper, "cleanup_scaled_photos");
        ASSERT_EQ(again.code, 200) << again.body;
        EXPECT_EQ(Json::Value::FromText(again.body)["scaled_photos_deleted"].Get<int64_t>(), 0);
    });
    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
