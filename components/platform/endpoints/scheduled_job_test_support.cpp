#include "endpoints/scheduled_job_test_support.h"

#include <string>

#include <gtest/gtest.h>

#include "business_logic/auth/cookie_manager_test_util.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/service_account.h"
#include "db_schema/people.h"
#include "db_schema/permissions.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/table_helpers/permissions.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/thread_pool.h"

namespace Endpoints::ScheduledJobTest {

namespace {

// Without a session length, a fresh token reads as expired.
void EnsureSessionLength(Transaction& transaction, EndpointTestHelper& helper) {
    auto secrets = helper.GetSecretsHelper();
    if (secrets->LookupSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros).empty()) {
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
                           std::to_string(30LL * 60LL * 1000000LL));
    }
}

// A session for `email`, set as the helper's cookie. Returns the person's id.
int64_t StartSession(Transaction& transaction, TestDatabaseUtil& testDb, EndpointTestHelper& helper,
                     std::string_view email) {
    std::string token;
    EXPECT_TRUE(Auth::PersonHelper(testDb.GetDatabaseHelper())
                    .CreateSessionToken(transaction, helper.GetSecretsHelper(), email, token));
    Auth::CookieProperties cookieProperties;
    cookieProperties.path = "/";
    cookieProperties.sameSite = Auth::CookieSameSitePolicy::None;
    helper.GetCookieManagerTest()->SetCookie("session_token", token, cookieProperties);
    return std::stoll(TableHelpers::People(testDb.GetDatabaseHelper())
                          .LookupPersonByEmail(transaction, email)
                          .at(std::string(DbSchema::kPeopleId)));
}

}  // namespace

int64_t SignInAsScheduler(Transaction& transaction, TestDatabaseUtil& testDb, EndpointTestHelper& helper) {
    DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
    EnsureSessionLength(transaction, helper);
    TableHelpers::Permissions(databaseHelper).AddPermission(
        transaction, DbSchema::kPermissionRunScheduledJobs, "Run the scheduled jobs");
    Auth::EnsureSchedulerServiceAccount(
        transaction, databaseHelper, kSchedulerEmail, "Scheduler-test-password-1", {});
    return StartSession(transaction, testDb, helper, kSchedulerEmail);
}

int64_t SignInAs(Transaction& transaction, TestDatabaseUtil& testDb, EndpointTestHelper& helper,
                 std::string_view email) {
    EnsureSessionLength(transaction, helper);
    Auth::PersonInfo info{ std::string(email), "Test", "Person" };
    Auth::PersonHelper(testDb.GetDatabaseHelper()).CreateFullyValidatedUser(transaction, info, "Password123!");
    return StartSession(transaction, testDb, helper, email);
}

crow::response RunJob(EndpointTestHelper& helper, std::string_view job) {
    crow::request req;
    req.method = crow::HTTPMethod::Post;
    req.url = "/api/admin/" + std::string(job);
    crow::response resp;
    helper.GetWebApp().GetApp().handle_full(req, resp);
    // The endpoint's async writes re-enter the test's connection, which isn't
    // thread-safe: let them finish before the test reads.
    ThreadPool::GetInstance().Shutdown();
    return resp;
}

int64_t NowUs(Transaction& transaction) {
    return std::stoll(transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));
}

}  // namespace Endpoints::ScheduledJobTest
