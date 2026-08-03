#include "upload_photo.h"

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
#include "db_schema/permissions.h"
#include "db_schema/roles.h"
#include "sql_util/table_helpers/admin_table_permissions.h"
#include "sql_util/table_helpers/permissions.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "sql_util/table_helpers/photo_support_tables.h"
#include "sql_util/table_helpers/roles.h"
#include "sql_util/table_helpers/role_assignments.h"
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

void SetupAdminUser(
    Transaction& transaction,
    TestDatabaseUtil& testDb,
    EndpointTestHelper& endpointHelper,
    int64_t& outPersonId) {
    auto secrets = endpointHelper.GetSecretsHelper();
    secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
        std::to_string(15LL * 60LL * 1000000LL));

    Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
    Auth::PersonInfo info{ "admin@example.com", "Admin", "User" };
    personHelper.CreateFullyValidatedUser(transaction, info, "Password123!");

    std::string sessionToken;
    ASSERT_TRUE(personHelper.CreateSessionToken(
        transaction, secrets, info.email, sessionToken));

    TableHelpers::People people(testDb.GetDatabaseHelper());
    KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
    outPersonId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

    TableHelpers::Roles roles(testDb.GetDatabaseHelper());
    int64_t adminRoleId = roles.AddRole(
        transaction, std::string(DbSchema::kRoleNameAdmin), "Administrator");
    TableHelpers::RoleAssignments ra(testDb.GetDatabaseHelper());
    ra.AddRoleAssignment(transaction, outPersonId, adminRoleId);

    auto cookieMgr = endpointHelper.GetCookieManagerTest();
    Auth::CookieProperties cp;
    cp.path = "/";
    cp.sameSite = Auth::CookieSameSitePolicy::None;
    cookieMgr->SetCookie("session_token", sessionToken, cp);
}

// Maps a table to an existing permission in admin_table_permissions — the
// "whoever administers this table" grant that photo writes now honor.
// GrantPermissionToPerson has already created the named permission.
void GrantTableToPermission(
    Transaction& transaction,
    TestDatabaseUtil& testDb,
    std::string_view tableName,
    std::string_view permissionName) {
    TableHelpers::Permissions permissions(testDb.GetDatabaseHelper());
    KeyValueTable row = permissions.GetPermission(transaction, permissionName);
    ASSERT_FALSE(row.empty());
    int64_t permissionId =
        std::stoll(row.at(std::string(DbSchema::kPermissionsId)));
    TableHelpers::AdminTablePermissions atp(testDb.GetDatabaseHelper());
    atp.AddAdminTablePermission(transaction, tableName, permissionId);
}

void SetupLoggedInUser(
    Transaction& transaction,
    TestDatabaseUtil& testDb,
    EndpointTestHelper& endpointHelper,
    int64_t& outPersonId) {
    auto secrets = endpointHelper.GetSecretsHelper();
    secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
        std::to_string(15LL * 60LL * 1000000LL));

    Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
    Auth::PersonInfo info{ "user@example.com", "Regular", "User" };
    personHelper.CreateFullyValidatedUser(transaction, info, "Password123!");

    std::string sessionToken;
    ASSERT_TRUE(personHelper.CreateSessionToken(
        transaction, secrets, info.email, sessionToken));

    TableHelpers::People people(testDb.GetDatabaseHelper());
    KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
    outPersonId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

    // Create admin role but do NOT assign it
    TableHelpers::Roles roles(testDb.GetDatabaseHelper());
    roles.AddRole(
        transaction, std::string(DbSchema::kRoleNameAdmin), "Administrator");

    auto cookieMgr = endpointHelper.GetCookieManagerTest();
    Auth::CookieProperties cp;
    cp.path = "/";
    cp.sameSite = Auth::CookieSameSitePolicy::None;
    cookieMgr->SetCookie("session_token", sessionToken, cp);
}

TEST(UploadPhotoTest, UploadPhotoSuccess) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadPhotoSuccess", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        int64_t personId = 0;
        SetupAdminUser(transaction, testDb, endpointHelper, personId);

        TableHelpers::PhotoSupportTables pst(testDb.GetDatabaseHelper());
        pst.AddPhotoSupportTable(transaction, "people");

        auto jpeg = MakeTestJpeg(64, 48);

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/upload_photo/people/" + std::to_string(personId)
            + "/jpeg";
        req.body.assign(jpeg.begin(), jpeg.end());
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);

        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_TRUE(body.HasChild("source_photo_id", nullptr));
        EXPECT_EQ(body["type"].Get<std::string>(), "jpeg");
        EXPECT_EQ(body["width"].Get<int64_t>(), 64);
        EXPECT_EQ(body["height"].Get<int64_t>(), 48);
    });

    Auth::ServerConfig::Shutdown();
}

TEST(UploadPhotoTest, UploadPhotoNotAdmin) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadPhotoNotAdmin", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        int64_t personId = 0;
        SetupLoggedInUser(transaction, testDb, endpointHelper, personId);

        auto jpeg = MakeTestJpeg(32, 32);

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/upload_photo/people/" + std::to_string(personId)
            + "/jpeg";
        req.body.assign(jpeg.begin(), jpeg.end());
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 403);
    });

    Auth::ServerConfig::Shutdown();
}

// A non-admin whose permissions grant the table through admin_table_permissions
// administers that table's rows, and therefore their photos. Before this, the
// generic upload was admin-only, which locked permission-scoped authors (e.g.
// author_blog) out of their own content's images.
TEST(UploadPhotoTest, UploadPhotoNonAdminWithTableGrant) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadPhotoGranted", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        int64_t personId = 0;
        SetupLoggedInUser(transaction, testDb, endpointHelper, personId);
        endpointHelper.GrantPermissionToPerson(
            transaction, personId, "author_things");
        GrantTableToPermission(transaction, testDb, "people", "author_things");

        TableHelpers::PhotoSupportTables pst(testDb.GetDatabaseHelper());
        pst.AddPhotoSupportTable(transaction, "people");

        auto jpeg = MakeTestJpeg(32, 32);

        crow::request req;
        req.method = crow::HTTPMethod::Post;
        req.url = "/api/upload_photo/people/" + std::to_string(personId)
            + "/jpeg";
        req.body.assign(jpeg.begin(), jpeg.end());
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200) << resp.body;
    });

    Auth::ServerConfig::Shutdown();
}

// The grant is per-table: holding a permission that unlocks one table says
// nothing about any other.
TEST(UploadPhotoTest, UploadPhotoNonAdminGrantIsPerTable) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadPhotoWrongTable", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        int64_t personId = 0;
        SetupLoggedInUser(transaction, testDb, endpointHelper, personId);
        endpointHelper.GrantPermissionToPerson(
            transaction, personId, "author_things");
        // Granted some OTHER table, not the one being written.
        GrantTableToPermission(transaction, testDb, "roles", "author_things");

        TableHelpers::PhotoSupportTables pst(testDb.GetDatabaseHelper());
        pst.AddPhotoSupportTable(transaction, "people");

        auto jpeg = MakeTestJpeg(32, 32);

        crow::request req;
        req.method = crow::HTTPMethod::Post;
        req.url = "/api/upload_photo/people/" + std::to_string(personId)
            + "/jpeg";
        req.body.assign(jpeg.begin(), jpeg.end());
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 403);
    });

    Auth::ServerConfig::Shutdown();
}

// REGRESSION GUARD. The obvious one-line version of this change is to swap the
// admin check for IsTableAllowed — and it is wrong. IsTableAllowed unions the
// app's BASE PUBLIC read list with the per-permission grants, so it would hand
// photo-write access on every public table to any logged-in user. Write access
// comes from the grants alone.
TEST(UploadPhotoTest, UploadPhotoBaseAllowedTableIsNotWritable) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadPhotoBaseAllowed", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        int64_t personId = 0;
        SetupLoggedInUser(transaction, testDb, endpointHelper, personId);
        // Readable by anyone (the app's base list) but granted to nobody.
        endpointHelper.AddAllowedTable(transaction, "people");

        TableHelpers::PhotoSupportTables pst(testDb.GetDatabaseHelper());
        pst.AddPhotoSupportTable(transaction, "people");

        auto jpeg = MakeTestJpeg(32, 32);

        crow::request req;
        req.method = crow::HTTPMethod::Post;
        // Someone ELSE's row, so the people self-photo carve-out doesn't apply.
        req.url = "/api/upload_photo/people/" + std::to_string(personId + 1)
            + "/jpeg";
        req.body.assign(jpeg.begin(), jpeg.end());
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 403);
    });

    Auth::ServerConfig::Shutdown();
}

TEST(UploadPhotoTest, UploadPhotoNotLoggedIn) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadPhotoNotLoggedIn", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        auto jpeg = MakeTestJpeg(32, 32);

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/upload_photo/people/1/jpeg";
        req.body.assign(jpeg.begin(), jpeg.end());
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 401);
    });

    Auth::ServerConfig::Shutdown();
}

TEST(UploadPhotoTest, UploadPhotoUnsupportedType) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadPhotoUnsupportedType", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        int64_t personId = 0;
        SetupAdminUser(transaction, testDb, endpointHelper, personId);

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/upload_photo/people/1/gif";
        req.body = "fake image data";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 400);
    });

    Auth::ServerConfig::Shutdown();
}

TEST(UploadPhotoTest, UploadPhotoUnsupportedTable) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadPhotoUnsupportedTable", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        int64_t personId = 0;
        SetupAdminUser(transaction, testDb, endpointHelper, personId);

        auto jpeg = MakeTestJpeg(32, 32);

        crow::request req;
        req.method = crow::HTTPMethod::POST;
        req.url = "/api/upload_photo/nonexistent/1/jpeg";
        req.body.assign(jpeg.begin(), jpeg.end());
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 400);
    });

    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
