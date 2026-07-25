#include "has_photo.h"

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
#include "business_logic/images/image_helper.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "sql_util/table_helpers/photo_support_tables.h"
#include "sql_util/table_helpers/people.h"
#include "test/src/util/database_test_helper.h"
#include "util/json_value.h"

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
    const std::string& email,
    int64_t& outPersonId) {
    auto secrets = endpointHelper.GetSecretsHelper();
    secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
        std::to_string(15LL * 60LL * 1000000LL));

    Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
    Auth::PersonInfo info{ email, "Test", "User" };
    personHelper.CreateFullyValidatedUser(transaction, info, "Password123!");

    std::string sessionToken;
    ASSERT_TRUE(personHelper.CreateSessionToken(
        transaction, secrets, info.email, sessionToken));

    TableHelpers::People people(testDb.GetDatabaseHelper());
    KeyValueTable person = people.LookupPersonByEmail(transaction, email);
    outPersonId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

    auto cookieMgr = endpointHelper.GetCookieManagerTest();
    Auth::CookieProperties cp;
    cp.path = "/";
    cp.sameSite = Auth::CookieSameSitePolicy::None;
    cookieMgr->SetCookie("session_token", sessionToken, cp);
}

TEST(HasPhotoTest, HasPhotoTrue) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HasPhotoTrue", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        int64_t personId = 0;
        SetupLoggedInUser(transaction, testDb, endpointHelper,
            "user@example.com", personId);

        // Upload a photo
        TableHelpers::PhotoSupportTables pst(testDb.GetDatabaseHelper());
        pst.AddPhotoSupportTable(transaction, "people");

        Images::ImageHelper imageHelper(testDb.GetDatabaseHelper());
        auto jpeg = MakeTestJpeg(32, 32);
        imageHelper.UploadAndAssociatePhoto(
            transaction, "people", personId, jpeg, "jpeg");

        crow::request req;
        req.method = crow::HTTPMethod::GET;
        req.url = "/api/has_photo/people/" + std::to_string(personId);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_TRUE(body["has_photo"].Get<bool>());
    });

    Auth::ServerConfig::Shutdown();
}

TEST(HasPhotoTest, HasPhotoFalse) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HasPhotoFalse", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        int64_t personId = 0;
        SetupLoggedInUser(transaction, testDb, endpointHelper,
            "user@example.com", personId);

        crow::request req;
        req.method = crow::HTTPMethod::GET;
        req.url = "/api/has_photo/people/" + std::to_string(personId);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_FALSE(body["has_photo"].Get<bool>());
    });

    Auth::ServerConfig::Shutdown();
}

TEST(HasPhotoTest, HasPhotoNotLoggedIn) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HasPhotoNotLoggedIn", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        crow::request req;
        req.method = crow::HTTPMethod::GET;
        req.url = "/api/has_photo/people/1";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 401);
    });

    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
