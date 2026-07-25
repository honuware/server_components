#include "get_photo.h"

#include <gtest/gtest.h>

#include <boost/gil.hpp>
#include <boost/gil/extension/io/jpeg.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>

#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/auth/cookie_manager_test_util.h"
#include "db_schema/people.h"
#include "db_schema/roles.h"
#include "business_logic/images/image_helper.h"
#include "util/error_codes.h"
#include "util/json_value.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "sql_util/table_helpers/photo_support_tables.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "sql_util/table_helpers/roles.h"
#include "test/src/util/database_test_helper.h"

namespace Endpoints {
namespace {

using VectorSink = boost::iostreams::back_insert_device<std::vector<char>>;
using VectorStream = boost::iostreams::stream<VectorSink>;

std::vector<char> MakeTestJpeg(int width, int height) {
    boost::gil::rgb8_image_t img(width, height);
    auto view = boost::gil::view(img);
    boost::gil::rgb8_pixel_t blue(0, 0, 255);
    for (int y = 0; y < view.height(); ++y) {
        auto iter = view.row_begin(y);
        for (int x = 0; x < view.width(); ++x) {
            iter[x] = blue;
        }
    }
    std::vector<char> result;
    VectorSink sink(result);
    VectorStream stream(sink);
    boost::gil::write_view(stream, boost::gil::const_view(img),
                           boost::gil::jpeg_tag());
    return result;
}

void SetupLoggedInUser(
    Transaction& transaction,
    TestDatabaseUtil& testDb,
    EndpointTestHelper& endpointHelper,
    int64_t& outPersonId,
    bool makeAdmin = true) {
    auto secrets = endpointHelper.GetSecretsHelper();
    secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
        std::to_string(15LL * 60LL * 1000000LL));

    Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
    Auth::PersonInfo info{ "user@example.com", "Test", "User" };
    personHelper.CreateFullyValidatedUser(transaction, info, "Password123!");

    std::string sessionToken;
    ASSERT_TRUE(personHelper.CreateSessionToken(
        transaction, secrets, info.email, sessionToken));

    TableHelpers::People people(testDb.GetDatabaseHelper());
    KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
    outPersonId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

    // Phase 3.7 of the security review: get_photo / get_scaled_photo now
    // require IsTableAllowed in addition to IsLoggedIn. Tests that use
    // the people table need the admin role + people in
    // admin_top_level_tables for the IsTableAllowed check to pass.
    if (makeAdmin) {
        TableHelpers::Roles roles(testDb.GetDatabaseHelper());
        int64_t adminRoleId = roles.AddRole(
            transaction, std::string(DbSchema::kRoleNameAdmin), "Administrator");
        TableHelpers::RoleAssignments ra(testDb.GetDatabaseHelper());
        ra.AddRoleAssignment(transaction, outPersonId, adminRoleId);
        endpointHelper.AddAdminTopLevelTable(
            transaction, DbSchema::kPeopleTable);
    }

    auto cookieMgr = endpointHelper.GetCookieManagerTest();
    Auth::CookieProperties cp;
    cp.path = "/";
    cp.sameSite = Auth::CookieSameSitePolicy::None;
    cookieMgr->SetCookie("session_token", sessionToken, cp);
}

TEST(GetPhotoTest, GetPhotoSuccess) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetPhotoSuccess", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        int64_t personId = 0;
        SetupLoggedInUser(transaction, testDb, endpointHelper, personId);

        // Upload a photo first
        TableHelpers::PhotoSupportTables pst(testDb.GetDatabaseHelper());
        pst.AddPhotoSupportTable(transaction, "people");

        Images::ImageHelper imageHelper(testDb.GetDatabaseHelper());
        auto jpeg = MakeTestJpeg(64, 48);
        imageHelper.UploadAndAssociatePhoto(
            transaction, "people", personId, jpeg, "jpeg");

        crow::request req;
        req.method = crow::HTTPMethod::GET;
        req.url = "/api/get_photo/people/" + std::to_string(personId);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);
        EXPECT_EQ(resp.get_header_value("Content-Type"), "image/jpeg");
        EXPECT_EQ(resp.get_header_value("Cache-Control"),
            "public, max-age=86400");
        EXPECT_FALSE(resp.body.empty());
    });

    Auth::ServerConfig::Shutdown();
}

TEST(GetPhotoTest, GetPhotoNotFound) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetPhotoNotFound", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        int64_t personId = 0;
        SetupLoggedInUser(transaction, testDb, endpointHelper, personId);

        crow::request req;
        req.method = crow::HTTPMethod::GET;
        req.url = "/api/get_photo/people/" + std::to_string(personId);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 404);
    });

    Auth::ServerConfig::Shutdown();
}

TEST(GetPhotoTest, GetPhotoNotLoggedIn) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetPhotoNotLoggedIn", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        crow::request req;
        req.method = crow::HTTPMethod::GET;
        req.url = "/api/get_photo/people/1";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 401);
    });

    Auth::ServerConfig::Shutdown();
}

// Phase 3.7 of the security review: a logged-in user without read
// access to the requested table must be rejected with the same error
// the generic CRUD endpoints use, NOT silently served the photo.
TEST(GetPhotoTest, GetPhotoForbiddenTable) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetPhotoForbiddenTable",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);

            int64_t personId = 0;
            // Logged in but NOT an admin — no people allow-list entry.
            SetupLoggedInUser(
                transaction, testDb, endpointHelper, personId,
                /*makeAdmin=*/false);

            crow::request req;
            req.method = crow::HTTPMethod::GET;
            req.url = "/api/get_photo/people/" + std::to_string(personId);
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

            EXPECT_EQ(resp.code, 400);
            Json::Value body = Json::Value::FromText(resp.body);
            EXPECT_EQ(body["type"].Get<std::string>(),
                ErrorCodes::kValidationError);
        });

    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
