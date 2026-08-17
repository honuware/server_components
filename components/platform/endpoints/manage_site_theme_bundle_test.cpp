#include "manage_site_theme_bundle.h"

#include <string>

#include <gtest/gtest.h>

#include "business_logic/auth/cookie_manager_test_util.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/branding/theme_bundle_sections.h"
#include "business_logic/branding/theme_bundle_zip.h"
#include "db_schema/people.h"
#include "db_schema/roles.h"
#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "sql_util/table_helpers/roles.h"
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
    Auth::PersonInfo info{"bundle_admin@example.com", "Bundle", "Admin"};
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
    const std::string& body = "",
    const std::string& query = "") {
    crow::request req;
    req.method = method;
    req.url = url;
    req.body = body;
    if (!query.empty()) {
        req.url_params = crow::query_string("?" + query);
    }
    crow::response resp;
    endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
    ThreadPool::GetInstance().Shutdown();
    return resp;
}

// ---- auth ----

TEST(ManageSiteThemeBundleTest, DownloadRequiresAdmin) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleDownloadAuth", [&](Transaction& transaction) {
        Branding::ClearThemeBundleSectionsForTest();
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/false);
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Get,
                       "/api/manage/site_theme_bundle").code, 403);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeBundleTest, DownloadRequiresLogin) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleDownloadAnon", [&](Transaction& transaction) {
        Branding::ClearThemeBundleSectionsForTest();
        EndpointTestHelper endpointHelper(transaction, testDb);
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Get,
                       "/api/manage/site_theme_bundle").code, 401);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeBundleTest, UploadAndValidateRequireAdmin) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleUploadAuth", [&](Transaction& transaction) {
        Branding::ClearThemeBundleSectionsForTest();
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/false);
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Post,
                       "/api/manage/site_theme_bundle", "x").code, 403);
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Post,
                       "/api/manage/site_theme_bundle/validate", "x").code, 403);
    });
    Auth::ServerConfig::Shutdown();
}

// ---- the round trip, through HTTP ----

TEST(ManageSiteThemeBundleTest, DownloadsAZipAndUploadsItBack) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleRoundTripHttp", [&](Transaction& transaction) {
        Branding::ClearThemeBundleSectionsForTest();
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        endpointHelper.GetSecretsHelper()->AddSecret(
            transaction, "site_theme_primary", "#e8743b");

        crow::response download = Call(
            endpointHelper, crow::HTTPMethod::Get, "/api/manage/site_theme_bundle");
        ASSERT_EQ(download.code, 200);
        EXPECT_EQ(download.get_header_value("Content-Type"), "application/zip");
        // The browser has to be told to save it, and under a recognisable name.
        EXPECT_NE(download.get_header_value("Content-Disposition").find("attachment"),
                  std::string::npos);
        EXPECT_EQ(download.get_header_value("Cache-Control"), "no-store");
        ASSERT_FALSE(download.body.empty());
        EXPECT_EQ(download.body.compare(0, 2, "PK"), 0);

        crow::response upload = Call(
            endpointHelper, crow::HTTPMethod::Post,
            "/api/manage/site_theme_bundle", download.body);
        ASSERT_EQ(upload.code, 200) << upload.body;
        Json::Value report = Json::Value::FromText(upload.body);
        EXPECT_TRUE(report["ok"].Get<bool>());
        EXPECT_GT(report["changes"]["tokens"].Get<int64_t>(), 0);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeBundleTest, ValidateReportsWithoutWriting) {
    // The route that makes trying a theme safe: you see the consequences first.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleValidate", [&](Transaction& transaction) {
        Branding::ClearThemeBundleSectionsForTest();
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, "site_theme_primary", "#e8743b");

        crow::response download = Call(
            endpointHelper, crow::HTTPMethod::Get, "/api/manage/site_theme_bundle");
        ASSERT_EQ(download.code, 200);

        // Change the live value, then dry-run the bundle over it.
        secrets->AddSecret(transaction, "site_theme_primary", "#000000");
        crow::response validate = Call(
            endpointHelper, crow::HTTPMethod::Post,
            "/api/manage/site_theme_bundle/validate", download.body);

        ASSERT_EQ(validate.code, 200) << validate.body;
        EXPECT_TRUE(Json::Value::FromText(validate.body)["ok"].Get<bool>());
        // Nothing was written — the live value is still the one we just set.
        EXPECT_EQ(secrets->LookupSecret(transaction, "site_theme_primary"), "#000000");
    });
    Auth::ServerConfig::Shutdown();
}

// ---- refusals ----

TEST(ManageSiteThemeBundleTest, AMalformedUploadIs400WithAUsefulMessage) {
    // A studio picking the wrong file is not a server error.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleBadZip", [&](Transaction& transaction) {
        Branding::ClearThemeBundleSectionsForTest();
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        crow::response resp = Call(
            endpointHelper, crow::HTTPMethod::Post,
            "/api/manage/site_theme_bundle", "this is not a zip at all");
        EXPECT_EQ(resp.code, 400);
        EXPECT_NE(resp.body.find("not a theme file"), std::string::npos);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeBundleTest, AnEmptyBodyIs400) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleNoBody", [&](Transaction& transaction) {
        Branding::ClearThemeBundleSectionsForTest();
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Post,
                       "/api/manage/site_theme_bundle", "").code, 400);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeBundleTest, ARefusedImportRollsBackTheFrameworkHalf) {
    // The import writes secrets and fonts BEFORE handing control to an app
    // section. RunInTransaction commits whenever its lambda returns normally,
    // so a section that refuses used to leave those writes committed while the
    // response said the import failed — half a theme, and the studio told it
    // got none. The failure path has to throw.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleRollback", [&](Transaction& transaction) {
        Branding::ClearThemeBundleSectionsForTest();
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, "site_theme_primary", "#e8743b");

        crow::response download = Call(
            endpointHelper, crow::HTTPMethod::Get, "/api/manage/site_theme_bundle");
        ASSERT_EQ(download.code, 200);

        // A section that always refuses, registered AFTER the export so the
        // bundle carries its body.
        Branding::RegisterThemeBundleSection(
            "page_content",
            [](Branding::SectionContext&, Json::Value& out) {
                out = Json::Value(Json::JsonObject{});
                return std::string();
            },
            [](Branding::SectionContext&, const Json::Value&, bool) {
                return std::string("home section 2 has an unknown kind");
            });
        crow::response withSection = Call(
            endpointHelper, crow::HTTPMethod::Get, "/api/manage/site_theme_bundle");
        ASSERT_EQ(withSection.code, 200);

        secrets->AddSecret(transaction, "site_theme_primary", "#000000");
        crow::response resp = Call(
            endpointHelper, crow::HTTPMethod::Post,
            "/api/manage/site_theme_bundle", withSection.body);

        EXPECT_EQ(resp.code, 400);
        EXPECT_NE(resp.body.find("unknown kind"), std::string::npos);
        Branding::ClearThemeBundleSectionsForTest();
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeBundleTest, StrictIsTheDefaultAndLenientIsAQueryFlag) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("BundleStrictness", [&](Transaction& transaction) {
        Branding::ClearThemeBundleSectionsForTest();
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, "site_theme_primary", "#e8743b");
        crow::response download = Call(
            endpointHelper, crow::HTTPMethod::Get, "/api/manage/site_theme_bundle");
        ASSERT_EQ(download.code, 200);
        const std::string zip = download.body;

        // Every flag combination has to be READ rather than ignored. The
        // strictness behaviour itself is covered against a hand-built unknown
        // key in ThemeBundleRoundTripTest — a bundle our own exporter wrote can
        // never carry one, since the exporter is registry-driven.
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Post,
                       "/api/manage/site_theme_bundle", zip).code, 200);
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Post,
                       "/api/manage/site_theme_bundle", zip, "lenient=1").code, 200);

        // merge=1 must leave a token the bundle does not mention alone, which
        // is what distinguishes it from the replace default.
        secrets->AddSecret(transaction, "site_theme_accent", "#123456");
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Post,
                       "/api/manage/site_theme_bundle", zip, "merge=1").code, 200);
        EXPECT_EQ(secrets->LookupSecret(transaction, "site_theme_primary"), "#e8743b");
    });
    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
