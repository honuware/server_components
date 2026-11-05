#include "get_scaled_photo.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <boost/gil.hpp>
#include <boost/gil/extension/io/jpeg.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>

#include "endpoints/endpoint_test_helper.h"
#include "endpoints/public_photo_tables.h"
#include "endpoints/web_app.h"
#include "business_logic/auth/cookie_manager_test_util.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/images/image_helper.h"
#include "db_schema/people.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/table_helpers/photo_support_tables.h"
#include "test/src/util/database_test_helper.h"
#include "util/error_codes.h"
#include "util/json_value.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"

// H15 — the per-row rule on PublicPhotoTables. A listed table serves every row to
// signed-out visitors unless the app's rule refuses the row; a refused row takes
// the signed-in path (login + IsTableAllowed, private cache headers). `people`
// stands in for an app table: the rule decides by row id.
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
    stream.flush();
    return result;
}

// A person row with a photo; returns its id.
int64_t AddPersonWithPhoto(
    Transaction& transaction, TestDatabaseUtil& testDb, const std::string& email) {
    TableHelpers::People people(testDb.GetDatabaseHelper());
    int64_t personId = people.AddPerson(transaction, email, "Test", "Person", "hash");
    TableHelpers::PhotoSupportTables photoSupportTables(testDb.GetDatabaseHelper());
    if (!photoSupportTables.IsPhotoSupportedForTable(transaction, DbSchema::kPeopleTable)) {
        photoSupportTables.AddPhotoSupportTable(transaction, std::string(DbSchema::kPeopleTable));
    }
    Images::ImageHelper(testDb.GetDatabaseHelper()).UploadAndAssociatePhoto(
        transaction, std::string(DbSchema::kPeopleTable), personId,
        MakeTestJpeg(64, 48), "jpeg");
    return personId;
}

// Lists `people` as public; with `publicId` set, the rule accepts only that row.
// Every call the rule receives lands in `calls`.
void ListPeople(EndpointTestHelper& endpointHelper,
                std::optional<int64_t> publicId,
                std::vector<std::pair<std::string, int64_t>>* calls = nullptr) {
    PublicPhotoTables publicPhotoTables{ { std::string(DbSchema::kPeopleTable) } };
    if (publicId) {
        publicPhotoTables.isRowPublic =
            [publicId, calls](const DatabaseHelper&, Transaction&,
                              std::string_view tableName, int64_t tableItemId) {
                if (calls) calls->emplace_back(std::string(tableName), tableItemId);
                return tableItemId == *publicId;
            };
    }
    endpointHelper.GetWebApp().SetService<PublicPhotoTables>(
        std::make_shared<PublicPhotoTables>(std::move(publicPhotoTables)));
}

// Signs a user in through the test cookie jar.
void SignIn(Transaction& transaction, TestDatabaseUtil& testDb,
            EndpointTestHelper& endpointHelper) {
    auto secrets = endpointHelper.GetSecretsHelper();
    secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
        std::to_string(15LL * 60LL * 1000000LL));
    Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
    Auth::PersonInfo info{ "reader@example.com", "Test", "Reader" };
    personHelper.CreateFullyValidatedUser(transaction, info, "Password123!");
    std::string sessionToken;
    ASSERT_TRUE(personHelper.CreateSessionToken(
        transaction, secrets, info.email, sessionToken));
    Auth::CookieProperties cp;
    cp.path = "/";
    cp.sameSite = Auth::CookieSameSitePolicy::None;
    endpointHelper.GetCookieManagerTest()->SetCookie("session_token", sessionToken, cp);
}

crow::response GetPersonPhoto(EndpointTestHelper& endpointHelper, int64_t personId) {
    crow::request req;
    req.method = crow::HTTPMethod::GET;
    req.url = "/api/get_scaled_photo/people/" + std::to_string(personId) + "/32/24";
    crow::response resp;
    endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
    return resp;
}

TEST(GetScaledPhotoTest, ListedTableWithoutARuleServesEveryRow) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ListedTableWithoutARuleServesEveryRow", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        int64_t personId = AddPersonWithPhoto(transaction, testDb, "one@example.com");
        ListPeople(endpointHelper, std::nullopt);

        crow::response resp = GetPersonPhoto(endpointHelper, personId);
        EXPECT_EQ(resp.code, 200) << resp.body;
        EXPECT_EQ(resp.get_header_value("Content-Type"), "image/jpeg");
        EXPECT_EQ(resp.get_header_value("Cache-Control"), "public, no-cache");
    });
    Auth::ServerConfig::Shutdown();
}

TEST(GetScaledPhotoTest, RowTheRuleAcceptsServesToSignedOutVisitors) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RowTheRuleAcceptsServesToSignedOutVisitors", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        int64_t personId = AddPersonWithPhoto(transaction, testDb, "one@example.com");
        std::vector<std::pair<std::string, int64_t>> calls;
        ListPeople(endpointHelper, personId, &calls);

        crow::response resp = GetPersonPhoto(endpointHelper, personId);
        EXPECT_EQ(resp.code, 200) << resp.body;
        EXPECT_EQ(resp.get_header_value("Cache-Control"), "public, no-cache");
        ASSERT_EQ(calls.size(), 1u);
        EXPECT_EQ(calls[0].first, DbSchema::kPeopleTable);
        EXPECT_EQ(calls[0].second, personId);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(GetScaledPhotoTest, RowTheRuleRefusesNeedsALogin) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RowTheRuleRefusesNeedsALogin", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        int64_t publicId = AddPersonWithPhoto(transaction, testDb, "one@example.com");
        int64_t privateId = AddPersonWithPhoto(transaction, testDb, "two@example.com");
        ListPeople(endpointHelper, publicId);

        EXPECT_EQ(GetPersonPhoto(endpointHelper, privateId).code, 401);
        EXPECT_EQ(GetPersonPhoto(endpointHelper, publicId).code, 200);
    });
    Auth::ServerConfig::Shutdown();
}

// The refused row still serves to a signed-in reader of the table, but never
// with headers that let the edge cache keep it.
TEST(GetScaledPhotoTest, RowTheRuleRefusesServesPrivatelyToAReader) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RowTheRuleRefusesServesPrivatelyToAReader", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        int64_t publicId = AddPersonWithPhoto(transaction, testDb, "one@example.com");
        int64_t privateId = AddPersonWithPhoto(transaction, testDb, "two@example.com");
        ListPeople(endpointHelper, publicId);
        SignIn(transaction, testDb, endpointHelper);
        endpointHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

        crow::response resp = GetPersonPhoto(endpointHelper, privateId);
        EXPECT_EQ(resp.code, 200) << resp.body;
        EXPECT_EQ(resp.get_header_value("Cache-Control"), "private, no-cache");
        // The accepted row stays public for a signed-in caller too.
        EXPECT_EQ(GetPersonPhoto(endpointHelper, publicId).get_header_value("Cache-Control"),
                  "public, no-cache");
    });
    Auth::ServerConfig::Shutdown();
}

TEST(GetScaledPhotoTest, RowTheRuleRefusesIsRefusedToASignedInStranger) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RowTheRuleRefusesIsRefusedToASignedInStranger", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        int64_t publicId = AddPersonWithPhoto(transaction, testDb, "one@example.com");
        int64_t privateId = AddPersonWithPhoto(transaction, testDb, "two@example.com");
        ListPeople(endpointHelper, publicId);
        SignIn(transaction, testDb, endpointHelper);

        crow::response resp = GetPersonPhoto(endpointHelper, privateId);
        EXPECT_EQ(resp.code, 400);
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
    });
    Auth::ServerConfig::Shutdown();
}

// An unlisted table is refused before the transaction; its rows never reach the rule.
TEST(GetScaledPhotoTest, UnlistedTableNeverReachesTheRule) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UnlistedTableNeverReachesTheRule", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        int calls = 0;
        PublicPhotoTables publicPhotoTables{ { "some_other_table" } };
        publicPhotoTables.isRowPublic = [&calls](const DatabaseHelper&, Transaction&,
                                                 std::string_view, int64_t) {
            ++calls;
            return true;
        };
        endpointHelper.GetWebApp().SetService<PublicPhotoTables>(
            std::make_shared<PublicPhotoTables>(std::move(publicPhotoTables)));

        EXPECT_EQ(GetPersonPhoto(endpointHelper, 1).code, 401);
        EXPECT_EQ(calls, 0);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(GetScaledPhotoTest, IsRowPublicNeedsTheTableListed) {
    PublicPhotoTables publicPhotoTables{ { "listed" } };
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsRowPublicNeedsTheTableListed", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        EXPECT_TRUE(publicPhotoTables.IsRowPublic(databaseHelper, transaction, "listed", 1));
        EXPECT_FALSE(publicPhotoTables.IsRowPublic(databaseHelper, transaction, "unlisted", 1));
        publicPhotoTables.isRowPublic = [](const DatabaseHelper&, Transaction&,
                                           std::string_view, int64_t id) { return id == 2; };
        EXPECT_FALSE(publicPhotoTables.IsRowPublic(databaseHelper, transaction, "listed", 1));
        EXPECT_TRUE(publicPhotoTables.IsRowPublic(databaseHelper, transaction, "listed", 2));
        EXPECT_FALSE(publicPhotoTables.IsRowPublic(databaseHelper, transaction, "unlisted", 2));
    });
}

}  // namespace
}  // namespace Endpoints
