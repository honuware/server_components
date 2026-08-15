#include "manage_site_fonts.h"

#include <string>

#include <gtest/gtest.h>

#include "business_logic/auth/cookie_manager_test_util.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "db_schema/people.h"
#include "db_schema/roles.h"
#include "db_schema/site_fonts.h"
#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "sql_util/table_helpers/roles.h"
#include "sql_util/table_helpers/site_fonts.h"
#include "test/src/util/database_test_helper.h"
#include "util/json_value.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/thread_pool.h"

namespace Endpoints {
namespace {

void SignIn(
    Transaction& transaction,
    TestDatabaseUtil& testDb,
    EndpointTestHelper& endpointHelper,
    bool makeAdmin) {
    DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
    auto secrets = endpointHelper.GetSecretsHelper();
    secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
                       std::to_string(15LL * 60LL * 1000000LL));

    Auth::PersonHelper personHelper(databaseHelper);
    Auth::PersonInfo info{"font_admin@example.com", "Font", "Admin"};
    personHelper.CreateFullyValidatedUser(transaction, info, "Password123!");

    std::string sessionToken;
    EXPECT_TRUE(personHelper.CreateSessionToken(
        transaction, secrets, info.email, sessionToken));

    TableHelpers::People people(databaseHelper);
    int64_t personId = std::stoll(
        people.LookupPersonByEmail(transaction, info.email)
            .at(std::string(DbSchema::kPeopleId)));

    TableHelpers::Roles roles(databaseHelper);
    int64_t adminRoleId = roles.AddRole(
        transaction, std::string(DbSchema::kRoleNameAdmin), "Administrator");
    if (makeAdmin) {
        TableHelpers::RoleAssignments assignments(databaseHelper);
        assignments.AddRoleAssignment(transaction, personId, adminRoleId);
    }

    Auth::CookieProperties properties;
    properties.path = "/";
    properties.sameSite = Auth::CookieSameSitePolicy::None;
    endpointHelper.GetCookieManagerTest()->SetCookie(
        "session_token", sessionToken, properties);
}

crow::response Call(
    EndpointTestHelper& endpointHelper,
    crow::HTTPMethod method,
    const std::string& url,
    const std::string& body = "") {
    crow::request req;
    req.method = method;
    req.url = url;
    req.body = body;
    crow::response resp;
    endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
    ThreadPool::GetInstance().Shutdown();
    return resp;
}

// A minimal payload with one Google-shaped source and one family on it.
constexpr const char* kGoodPayload = R"JSON({
  "sources": [{
    "source_key": "google",
    "display_name": "Google Fonts",
    "base_url": "https://fonts.googleapis.com/css2",
    "query_suffix": "display=swap",
    "preconnect_lines": "https://fonts.googleapis.com|false\nhttps://fonts.gstatic.com|true"
  }],
  "families": [{
    "family": "Barlow", "fallback": "sans-serif", "source_kind": "cdn",
    "source_key": "google", "spec": "family=Barlow:wght@100..900"
  }]
})JSON";

// ---- auth ----

TEST(ManageSiteFontsTest, RequiresAdmin) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsNonAdmin", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/false);
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Get,
                       "/api/manage/site_fonts").code, 403);
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Put,
                       "/api/manage/site_fonts", kGoodPayload).code, 403);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteFontsTest, RequiresLogin) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsAnon", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Get,
                       "/api/manage/site_fonts").code, 401);
    });
    Auth::ServerConfig::Shutdown();
}

// ---- round trip ----

TEST(ManageSiteFontsTest, SavesAndReadsBackSourcesAndFamilies) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsRoundTrip", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Put,
                       "/api/manage/site_fonts", kGoodPayload).code, 200);

        Json::Value body = Json::Value::FromText(
            Call(endpointHelper, crow::HTTPMethod::Get,
                 "/api/manage/site_fonts").body);
        ASSERT_EQ(body["sources"].GetArray().size(), 1u);
        EXPECT_EQ(body["sources"][0]["base_url"].Get<std::string>(),
                  "https://fonts.googleapis.com/css2");
        EXPECT_EQ(body["sources"][0]["query_suffix"].Get<std::string>(),
                  "display=swap");
        ASSERT_EQ(body["families"].GetArray().size(), 1u);
        EXPECT_EQ(body["families"][0]["family"].Get<std::string>(), "Barlow");
        EXPECT_EQ(body["families"][0]["fallback"].Get<std::string>(), "sans-serif");
        EXPECT_EQ(body["families"][0]["spec"].Get<std::string>(),
                  "family=Barlow:wght@100..900");
        // The family points at the source that was just created.
        EXPECT_NE(body["families"][0]["font_source_id"].Get<std::string>(), "");
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteFontsTest, ReplacesRatherThanAccumulating) {
    // The PUT is a whole-inventory replace: a family the studio removed in the
    // editor must actually be gone, not merely absent from the payload.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsReplace", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
        fonts.AddFont(transaction, "Removed", "serif",
                      DbSchema::kSiteFontSourceKindSystem, 0, "", 10);

        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Put,
                       "/api/manage/site_fonts", kGoodPayload).code, 200);

        Json::Value body = Json::Value::FromText(
            Call(endpointHelper, crow::HTTPMethod::Get,
                 "/api/manage/site_fonts").body);
        ASSERT_EQ(body["families"].GetArray().size(), 1u);
        EXPECT_EQ(body["families"][0]["family"].Get<std::string>(), "Barlow");
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteFontsTest, EditingOneFamilyKeepsAnotherFamilysUploadedFaces) {
    // THE bug this reconciliation exists to prevent. The save used to delete
    // every family and re-add it, which threw away the uploaded font FILES
    // hanging off each row — so renaming an unrelated family silently destroyed
    // the studio's brand typeface and there was nothing to re-upload from.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsKeepFaces", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
        int64_t fontId = fonts.AddFont(
            transaction, "Studio Sans", "sans-serif",
            DbSchema::kSiteFontSourceKindUploaded, 0, "", 10);
        int64_t faceId = fonts.AddFace(
            transaction, fontId, 400, "normal", "woff2", "wOF2fake-bytes");

        // A save that leaves Studio Sans listed but changes something else.
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Put,
                       "/api/manage/site_fonts",
                       R"JSON({"sources":[],"families":[
                         {"family":"Studio Sans","fallback":"sans-serif",
                          "source_kind":"uploaded"},
                         {"family":"Georgia","fallback":"serif",
                          "source_kind":"system"}]})JSON").code, 200);

        // The family kept its row id, so the uploaded file is still attached.
        EXPECT_FALSE(fonts.GetFace(transaction, faceId).empty());
        KeyValueTable row = fonts.GetFontByFamily(transaction, "Studio Sans");
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(row.at(std::string(DbSchema::kSiteFontId)),
                  std::to_string(fontId));
        ASSERT_EQ(fonts.GetFacesForFont(transaction, fontId).size(), 1u);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteFontsTest, EditingASourceKeepsTheFamiliesPointedAtIt) {
    // Sources are matched by key for the same reason: rebuilding the row would
    // hand it a new id, and every family referencing it would be re-pointed at
    // a source that no longer exists.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsSourceEdit", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Put,
                       "/api/manage/site_fonts", kGoodPayload).code, 200);
        TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
        int64_t sourceId = std::stoll(
            fonts.GetSourceByKey(transaction, "google")
                .at(std::string(DbSchema::kSiteFontSourceId)));

        // Rename the source's display name; the key is what identifies it.
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Put,
                       "/api/manage/site_fonts",
                       R"JSON({"sources":[{
                         "source_key":"google","display_name":"Google",
                         "base_url":"https://fonts.googleapis.com/css2",
                         "query_suffix":"display=swap","preconnect_lines":""}],
                         "families":[{"family":"Barlow","fallback":"sans-serif",
                          "source_kind":"cdn","source_key":"google",
                          "spec":"family=Barlow:wght@100..900"}]})JSON").code, 200);

        KeyValueTable source = fonts.GetSourceByKey(transaction, "google");
        ASSERT_FALSE(source.empty());
        EXPECT_EQ(source.at(std::string(DbSchema::kSiteFontSourceId)),
                  std::to_string(sourceId));
        EXPECT_EQ(source.at(std::string(DbSchema::kSiteFontSourceDisplayName)),
                  "Google");
        EXPECT_EQ(fonts.GetFontByFamily(transaction, "Barlow")
                      .at(std::string(DbSchema::kSiteFontFontSourceId)),
                  std::to_string(sourceId));
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteFontsTest, AFamilyThatStopsBeingDownloadedLetsGoOfItsSource) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsDropSource", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Put,
                       "/api/manage/site_fonts", kGoodPayload).code, 200);
        // Barlow becomes a system family — it must not keep pointing at Google.
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Put,
                       "/api/manage/site_fonts",
                       R"JSON({"sources":[],"families":[
                         {"family":"Barlow","fallback":"sans-serif",
                          "source_kind":"system"}]})JSON").code, 200);

        TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
        KeyValueTable row = fonts.GetFontByFamily(transaction, "Barlow");
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(row.at(std::string(DbSchema::kSiteFontSourceKind)), "system");
        // Cleared, not left dangling at the deleted source's id.
        auto it = row.find(std::string(DbSchema::kSiteFontFontSourceId));
        EXPECT_TRUE(it == row.end() || it->second.empty());
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteFontsTest, ASystemFamilyNeedsNoSourceOrSpec) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsSystem", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Put,
                       "/api/manage/site_fonts",
                       R"JSON({"sources":[],"families":[
                         {"family":"Georgia","fallback":"serif",
                          "source_kind":"system"}]})JSON").code, 200);

        Json::Value body = Json::Value::FromText(
            Call(endpointHelper, crow::HTTPMethod::Get,
                 "/api/manage/site_fonts").body);
        ASSERT_EQ(body["families"].GetArray().size(), 1u);
        EXPECT_EQ(body["families"][0]["source_kind"].Get<std::string>(), "system");
    });
    Auth::ServerConfig::Shutdown();
}

// ---- validation ----

TEST(ManageSiteFontsTest, RefusesANonHttpsSourceUrl) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsInsecure", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        crow::response resp = Call(
            endpointHelper, crow::HTTPMethod::Put, "/api/manage/site_fonts",
            R"JSON({"sources":[{"source_key":"bad","display_name":"Bad",
                     "base_url":"http://fonts.example/css"}],"families":[]})JSON");
        EXPECT_EQ(resp.code, 400);
        EXPECT_NE(resp.body.find("https"), std::string::npos) << resp.body;
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteFontsTest, RefusesAFamilyWithNoFallback) {
    // D13 is the reason: the fallback is data and is never assumed, so a family
    // without one is a configuration error rather than something to guess at.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsNoFallback", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        crow::response resp = Call(
            endpointHelper, crow::HTTPMethod::Put, "/api/manage/site_fonts",
            R"JSON({"sources":[],"families":[
              {"family":"Barlow","fallback":"","source_kind":"system"}]})JSON");
        EXPECT_EQ(resp.code, 400);
        EXPECT_NE(resp.body.find("fallback"), std::string::npos) << resp.body;
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteFontsTest, RefusesASpecThatSmugglesAnExtraParameter) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsBadSpec", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        crow::response resp = Call(
            endpointHelper, crow::HTTPMethod::Put, "/api/manage/site_fonts",
            R"JSON({"sources":[{"source_key":"google","display_name":"G",
                     "base_url":"https://fonts.googleapis.com/css2"}],
                    "families":[{"family":"Barlow","fallback":"sans-serif",
                     "source_kind":"cdn","source_key":"google",
                     "spec":"family=Barlow&evil=1"}]})JSON");
        EXPECT_EQ(resp.code, 400);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteFontsTest, RefusesTheWholePayloadRatherThanApplyingItHalfway) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsAtomic", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
        fonts.AddFont(transaction, "Existing", "serif",
                      DbSchema::kSiteFontSourceKindSystem, 0, "", 10);

        // Good family first, bad one second.
        crow::response resp = Call(
            endpointHelper, crow::HTTPMethod::Put, "/api/manage/site_fonts",
            R"JSON({"sources":[],"families":[
              {"family":"Fine","fallback":"serif","source_kind":"system"},
              {"family":"Broken","fallback":"","source_kind":"system"}]})JSON");
        EXPECT_EQ(resp.code, 400);
        // The existing inventory must be untouched — nothing was deleted.
        auto families = fonts.GetAllFonts(transaction);
        ASSERT_EQ(families.size(), 1u);
        EXPECT_EQ(families[0].at("family"), "Existing");
    });
    Auth::ServerConfig::Shutdown();
}

// ---- face upload (D14) ----

TEST(ManageSiteFontsTest, UploadsAFaceAndDecidesFormatFromMagicBytes) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsUpload", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
        int64_t fontId = fonts.AddFont(
            transaction, "Studio Sans", "sans-serif",
            DbSchema::kSiteFontSourceKindUploaded, 0, "", 10);

        crow::response resp = Call(
            endpointHelper, crow::HTTPMethod::Post,
            "/api/manage/site_font_face/" + std::to_string(fontId) + "/700/italic",
            "wOF2 pretend font bytes");
        EXPECT_EQ(resp.code, 200);

        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["format"].Get<std::string>(), "woff2");
        EXPECT_EQ(body["weight"].Get<int64_t>(), 700);
        EXPECT_EQ(body["style"].Get<std::string>(), "italic");

        auto faces = fonts.GetFacesForFont(transaction, fontId);
        ASSERT_EQ(faces.size(), 1u);
        EXPECT_EQ(faces[0].at("format"), "woff2");
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteFontsTest, RefusesAnUploadThatIsNotAFont) {
    // The security-relevant one: bytes are served back from our own origin, so
    // the filename must never be what decides they are a font.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsUploadJunk", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
        int64_t fontId = fonts.AddFont(
            transaction, "Studio Sans", "sans-serif",
            DbSchema::kSiteFontSourceKindUploaded, 0, "", 10);

        crow::response resp = Call(
            endpointHelper, crow::HTTPMethod::Post,
            "/api/manage/site_font_face/" + std::to_string(fontId) + "/400/normal",
            "<script>alert(1)</script>");
        EXPECT_EQ(resp.code, 400);
        EXPECT_TRUE(fonts.GetFacesForFont(transaction, fontId).empty());
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteFontsTest, RefusesAnUploadForAFamilyThatDoesNotExist) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsUploadNoFamily", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Post,
                       "/api/manage/site_font_face/999999/400/normal",
                       "wOF2 bytes").code, 404);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteFontsTest, DeletesAFace) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsDeleteFace", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
        int64_t fontId = fonts.AddFont(
            transaction, "Studio Sans", "sans-serif",
            DbSchema::kSiteFontSourceKindUploaded, 0, "", 10);
        int64_t faceId = fonts.AddFace(
            transaction, fontId, 400, "normal", "woff2", "wOF2bytes");

        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Delete,
                       "/api/manage/site_font_face/" + std::to_string(faceId)).code,
                  200);
        EXPECT_TRUE(fonts.GetFacesForFont(transaction, fontId).empty());
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Delete,
                       "/api/manage/site_font_face/" + std::to_string(faceId)).code,
                  404);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteFontsTest, RemovingAFamilyTakesItsFacesWithIt) {
    // site_font_faces references site_fonts, so a replace that dropped a family
    // without first dropping its faces would violate the foreign key.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontsCascade", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
        int64_t fontId = fonts.AddFont(
            transaction, "Doomed", "sans-serif",
            DbSchema::kSiteFontSourceKindUploaded, 0, "", 10);
        fonts.AddFace(transaction, fontId, 400, "normal", "woff2", "wOF2bytes");

        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Put,
                       "/api/manage/site_fonts",
                       R"JSON({"sources":[],"families":[]})JSON").code, 200);
        EXPECT_TRUE(fonts.GetAllFonts(transaction).empty());
        EXPECT_TRUE(fonts.GetFacesForFont(transaction, fontId).empty());
    });
    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
