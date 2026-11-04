#pragma once

#include <cstdint>
#include <string_view>

#include <crow.h>

#include "endpoints/endpoint_test_helper.h"
#include "test/src/util/database_test_helper.h"

// Shared setup for the framework's scheduled-job endpoint tests
// (scheduled_job_handler_test and the admin_cleanup_* /
// admin_notify_admin_alerts tests). Plain helpers each test calls at its
// top — not a fixture.
namespace Endpoints::ScheduledJobTest {

// The neutral test identity of the scheduler service account (an app passes
// its own, e.g. App::kSchedulerServiceAccountEmail).
inline constexpr std::string_view kSchedulerEmail = "scheduler@svc.test";

// The scheduler service account, provisioned the way a seed does it
// (EnsureSchedulerServiceAccount with no app permissions: its role holds only
// run_scheduled_jobs), then signed in on the helper's cookie manager, so later
// requests are the scheduler's. The harness doesn't run the framework seed, so
// the permission row is added first. At most once per test. Returns its person
// id.
int64_t SignInAsScheduler(Transaction& transaction, TestDatabaseUtil& testDb, EndpointTestHelper& helper);

// A new, fully validated person with no permissions, signed in in place of
// whoever was. Returns its person id.
int64_t SignInAs(Transaction& transaction, TestDatabaseUtil& testDb, EndpointTestHelper& helper,
                 std::string_view email);

// POST /api/admin/<job> with an empty body, as the scheduler sends it. Waits
// for the endpoint's async writes before returning, so the caller can read the
// database.
crow::response RunJob(EndpointTestHelper& helper, std::string_view job);

// The database's clock (now_us()).
int64_t NowUs(Transaction& transaction);

}  // namespace Endpoints::ScheduledJobTest
