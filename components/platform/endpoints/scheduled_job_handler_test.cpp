#include "endpoints/scheduled_job_handler.h"

#include <string>

#include <gtest/gtest.h>

#include "business_logic/auth/server_config.h"
#include "business_logic/scheduled_jobs/platform_jobs.h"
#include "db_schema/permissions.h"
#include "endpoints/scheduled_job_test_support.h"

// H13 — the gate every scheduled job shares, checked across the framework's
// five routes. Each job's own file tests its counts.
namespace Endpoints {
namespace {

using namespace ScheduledJobTest;

TEST(ScheduledJobHandlerTest, EveryJobIs401SignedOutAnd403WithoutThePermission) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EveryJobIs401SignedOutAnd403", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        for (std::string_view job : PlatformJobs::PlatformJobNames()) {
            EXPECT_EQ(RunJob(helper, job).code, 401) << job;
        }

        // The permission exists (the scheduler holds it); this member doesn't.
        SignInAsScheduler(transaction, testDb, helper);
        SignInAs(transaction, testDb, helper, "member@example.com");
        for (std::string_view job : PlatformJobs::PlatformJobNames()) {
            EXPECT_EQ(RunJob(helper, job).code, 403) << job;
        }
    });
    Auth::ServerConfig::Shutdown();
}

// Every route is registered (anchored in RegisterFrameworkEndpoints) and
// answers the service account, even with nothing to do.
TEST(ScheduledJobHandlerTest, EveryJobAnswersTheSchedulerOnAnEmptyDatabase) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EveryJobAnswersTheScheduler", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        SignInAsScheduler(transaction, testDb, helper);
        for (std::string_view job : PlatformJobs::PlatformJobNames()) {
            crow::response resp = RunJob(helper, job);
            EXPECT_EQ(resp.code, 200) << job << ": " << resp.body;
            EXPECT_EQ(resp.get_header_value("Content-Type"), "application/json") << job;
        }
    });
    Auth::ServerConfig::Shutdown();
}

// Administrator holds run_scheduled_jobs too (the framework seed), so a job
// can be run by hand: anyone holding the permission can.
TEST(ScheduledJobHandlerTest, AnyoneHoldingThePermissionCanRunAJobByHand) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AnyoneHoldingThePermission", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        int64_t adminId = SignInAs(transaction, testDb, helper, "admin@example.com");
        helper.GrantPermissionToPerson(transaction, adminId, DbSchema::kPermissionRunScheduledJobs);

        crow::response resp = RunJob(helper, PlatformJobs::kJobCleanupIdempotencyKeys);
        ASSERT_EQ(resp.code, 200) << resp.body;
        EXPECT_EQ(Json::Value::FromText(resp.body)["idempotency_keys_deleted"].Get<int64_t>(), 0);
    });
    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
