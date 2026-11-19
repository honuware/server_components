#include "image_upload_settings.h"

#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "business_logic/auth/server_config.h"
#include "db_schema/permissions.h"
#include "db_schema/roles.h"
#include "endpoints/endpoint_test_helper.h"
#include "endpoints/scheduled_job_test_support.h"
#include "endpoints/web_app.h"
#include "sql_util/table_helpers/photo_support_tables.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "sql_util/table_helpers/roles.h"
#include "test/src/util/database_test_helper.h"
#include "util/json_value.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/thread_pool.h"

// The image upload limit's routes (CommunityFinder CM3 Phase 2 §13.1).
namespace Endpoints {
namespace {

using ::testing::HasSubstr;
using ScheduledJobTest::SignInAs;

constexpr int64_t kMegabyte = 1024 * 1024;
constexpr std::string_view kLimitUrl = "/api/image_upload_limit";
constexpr std::string_view kSettingsUrl = "/api/admin/image_upload_settings";

crow::response Call(
    EndpointTestHelper& helper,
    crow::HTTPMethod method,
    std::string_view url,
    const std::string& body = "") {
    crow::request req;
    req.method = method;
    req.url = std::string(url);
    req.body = body;
    crow::response resp;
    helper.GetWebApp().GetApp().handle_full(req, resp);
    // The session touch's async writes re-enter the test's connection, which
    // isn't thread-safe: let them finish before the test reads.
    ThreadPool::GetInstance().Shutdown();
    return resp;
}

int64_t LimitIn(const crow::response& resp) {
    return Json::Value::FromText(resp.body)["max_upload_bytes"].Get<int64_t>();
}

std::string PutBody(std::string_view value) {
    return "{\"max_upload_bytes\": " + std::string(value) + "}";
}

// Signed in, holding admin_portal.
int64_t SignInAsAdministrator(
    Transaction& transaction, TestDatabaseUtil& testDb, EndpointTestHelper& helper) {
    const int64_t personId = SignInAs(transaction, testDb, helper, "site_admin@example.com");
    helper.GrantPermissionToPerson(transaction, personId, DbSchema::kPermissionAdminPortal);
    return personId;
}

TEST(ImageUploadSettingsTest, EveryRouteNeedsASignIn) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadSettingsSignedOut", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        EXPECT_EQ(Call(helper, crow::HTTPMethod::Get, kLimitUrl).code, 401);
        EXPECT_EQ(Call(helper, crow::HTTPMethod::Get, kSettingsUrl).code, 401);
        EXPECT_EQ(Call(helper, crow::HTTPMethod::Put, kSettingsUrl, PutBody("20971520")).code,
                  401);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ImageUploadSettingsTest, AnyoneSignedInCanReadTheLimit) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadSettingsMemberRead", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        SignInAs(transaction, testDb, helper, "member@example.com");

        crow::response resp = Call(helper, crow::HTTPMethod::Get, kLimitUrl);
        ASSERT_EQ(resp.code, 200);
        EXPECT_EQ(LimitIn(resp), 10 * kMegabyte);

        helper.GetSecretsHelper()->AddSecretTest(Secrets::kImageMaxUploadBytes, "20971520");
        EXPECT_EQ(LimitIn(Call(helper, crow::HTTPMethod::Get, kLimitUrl)), 20 * kMegabyte);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ImageUploadSettingsTest, TheSettingsAreForAdministratorsOnly) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadSettingsMember", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        // admin_portal exists (someone holds it); this member doesn't.
        SignInAsAdministrator(transaction, testDb, helper);
        SignInAs(transaction, testDb, helper, "member@example.com");

        EXPECT_EQ(Call(helper, crow::HTTPMethod::Get, kSettingsUrl).code, 403);
        EXPECT_EQ(Call(helper, crow::HTTPMethod::Put, kSettingsUrl, PutBody("20971520")).code,
                  403);
        EXPECT_EQ(helper.GetSecretsHelper()->LookupSecretTest(Secrets::kImageMaxUploadBytes),
                  "10485760");
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ImageUploadSettingsTest, AnAdministratorSeesTheLimitAndItsRange) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadSettingsRead", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        SignInAsAdministrator(transaction, testDb, helper);

        crow::response resp = Call(helper, crow::HTTPMethod::Get, kSettingsUrl);
        ASSERT_EQ(resp.code, 200);
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["max_upload_bytes"].Get<int64_t>(), 10 * kMegabyte);
        EXPECT_EQ(body["default_bytes"].Get<int64_t>(), 10 * kMegabyte);
        EXPECT_EQ(body["minimum_bytes"].Get<int64_t>(), 1 * kMegabyte);
        EXPECT_EQ(body["maximum_bytes"].Get<int64_t>(), 50 * kMegabyte);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ImageUploadSettingsTest, AnAdministratorsNewLimitIsWhatEveryoneReads) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadSettingsSave", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        SignInAsAdministrator(transaction, testDb, helper);

        crow::response resp =
            Call(helper, crow::HTTPMethod::Put, kSettingsUrl, PutBody("26214400"));
        ASSERT_EQ(resp.code, 200);
        EXPECT_EQ(LimitIn(resp), 25 * kMegabyte);
        EXPECT_EQ(Json::Value::FromText(resp.body)["maximum_bytes"].Get<int64_t>(),
                  50 * kMegabyte);
        EXPECT_EQ(helper.GetSecretsHelper()->LookupSecretTest(Secrets::kImageMaxUploadBytes),
                  "26214400");

        SignInAs(transaction, testDb, helper, "member@example.com");
        EXPECT_EQ(LimitIn(Call(helper, crow::HTTPMethod::Get, kLimitUrl)), 25 * kMegabyte);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ImageUploadSettingsTest, ALimitOutOfRangeOrNotAWholeNumberIsRefused) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadSettingsRefused", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        SignInAsAdministrator(transaction, testDb, helper);

        for (const char* tooSmallOrBig : {"1048575", "52428801", "0", "-1"}) {
            crow::response resp =
                Call(helper, crow::HTTPMethod::Put, kSettingsUrl, PutBody(tooSmallOrBig));
            EXPECT_EQ(resp.code, 400) << tooSmallOrBig;
            EXPECT_THAT(resp.body, HasSubstr("The upload limit must be from 1 to 50 MB."))
                << tooSmallOrBig;
        }
        for (const char* notAWholeNumber : {"\"20971520\"", "20971520.5", "null", "true"}) {
            crow::response resp =
                Call(helper, crow::HTTPMethod::Put, kSettingsUrl, PutBody(notAWholeNumber));
            EXPECT_EQ(resp.code, 400) << notAWholeNumber;
            EXPECT_THAT(resp.body, HasSubstr("must be a whole number of bytes"))
                << notAWholeNumber;
        }
        for (const char* noLimit : {"", "{}", "not json", "[20971520]"}) {
            EXPECT_EQ(Call(helper, crow::HTTPMethod::Put, kSettingsUrl, noLimit).code, 400)
                << noLimit;
        }
        EXPECT_EQ(helper.GetSecretsHelper()->LookupSecretTest(Secrets::kImageMaxUploadBytes),
                  "10485760");
    });
    Auth::ServerConfig::Shutdown();
}

// The upload routes keep checking the limit themselves, and stay the last word.
TEST(ImageUploadSettingsTest, TheUploadRouteEnforcesASavedLimit) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadSettingsEnforced", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        const int64_t personId = SignInAsAdministrator(transaction, testDb, helper);
        // Writing a table's photos takes the admin role.
        TableHelpers::Roles roles(testDb.GetDatabaseHelper());
        const int64_t adminRoleId = roles.AddRole(
            transaction, std::string(DbSchema::kRoleNameAdmin), "Administrator");
        TableHelpers::RoleAssignments(testDb.GetDatabaseHelper())
            .AddRoleAssignment(transaction, personId, adminRoleId);
        TableHelpers::PhotoSupportTables(testDb.GetDatabaseHelper())
            .AddPhotoSupportTable(transaction, "people");

        ASSERT_EQ(Call(helper, crow::HTTPMethod::Put, kSettingsUrl, PutBody("1048576")).code,
                  200);
        // The size is checked before the image is read, so the bytes needn't
        // be a real image.
        crow::response resp = Call(
            helper, crow::HTTPMethod::Post,
            "/api/upload_photo/people/" + std::to_string(personId) + "/jpeg",
            std::string(static_cast<size_t>(kMegabyte + 1), 'x'));
        EXPECT_EQ(resp.code, 400);
        EXPECT_THAT(resp.body, HasSubstr("Image exceeds maximum upload size"));
    });
    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
