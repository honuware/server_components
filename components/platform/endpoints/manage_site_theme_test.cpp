#include "manage_site_theme.h"

#include <string>

#include <gtest/gtest.h>

#include "business_logic/auth/cookie_manager_test_util.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/branding/site_content_slots.h"
#include "business_logic/branding/site_theme_tokens.h"
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

// Signs a person in and sets the session cookie the endpoint will read.
// `makeAdmin` decides whether they carry the admin role — the whole point of
// several tests below.
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
    Auth::PersonInfo info{"theme_admin@example.com", "Theme", "Admin"};
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
    const std::string& body = "") {
    crow::request req;
    req.method = method;
    req.url = "/api/manage/site_theme";
    req.body = body;
    crow::response resp;
    endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
    // /api/me-style session touches queue async writes onto the SAME libpqxx
    // connection this test thread uses; flush before any DB read.
    ThreadPool::GetInstance().Shutdown();
    return resp;
}

// Finds a field in the GET response's `content` / `theme` arrays.
const Json::Value* Field(
    const Json::Value& body, const char* section, const std::string& key) {
    const Json::Value* list = nullptr;
    if (!body.HasChild(section, &list) || !list->IsArray()) {
        return nullptr;
    }
    for (const auto& entry : list->GetArray()) {
        const Json::Value* keyValue = nullptr;
        if (entry.HasChild("key", &keyValue) &&
            keyValue->Get<std::string>() == key) {
            return &entry;
        }
    }
    return nullptr;
}

// ---- auth ----

TEST(ManageSiteThemeTest, RequiresLogin) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemeAnon", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Get).code, 401);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeTest, RequiresAdminNotJustAnAccount) {
    // config_secrets is excluded from the generic CRUD because it holds live
    // credentials; this curated surface must not become the way around that.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemeNonAdmin", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/false);
        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Get).code, 403);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeTest, PutRequiresAdmin) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemePutNonAdmin", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/false);
        // Assert the value is UNCHANGED rather than empty: this suite also runs
        // inside an app's test binary, where the app's own brand defaults are
        // pre-loaded into the secrets double.
        auto secrets = endpointHelper.GetSecretsHelper();
        std::string before = secrets->LookupSecretTest("site_hero_headline");

        crow::response resp = Call(
            endpointHelper, crow::HTTPMethod::Put,
            R"({"content":{"site_hero_headline":"Sneaky"}})");
        EXPECT_EQ(resp.code, 403);
        EXPECT_EQ(secrets->LookupSecretTest("site_hero_headline"), before);
    });
    Auth::ServerConfig::Shutdown();
}

// ---- GET ----

TEST(ManageSiteThemeTest, ServesEverySlotAndTokenToAnAdmin) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemeGet", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        crow::response resp = Call(endpointHelper, crow::HTTPMethod::Get);
        EXPECT_EQ(resp.code, 200);

        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["content"].GetArray().size(),
                  Branding::SiteContentSlots().size());
        EXPECT_EQ(body["theme"].GetArray().size(),
                  Branding::SiteThemeTokens().size());
        EXPECT_TRUE(body.HasChild("font_families", nullptr));

        // Each field carries the control type the editor switches on.
        const Json::Value* about = Field(body, "content", "site_about_markdown");
        ASSERT_TRUE(about != nullptr);
        EXPECT_EQ((*about)["type"].Get<std::string>(), "markdown");
        const Json::Value* radius = Field(body, "theme", "site_theme_radius_card");
        ASSERT_TRUE(radius != nullptr);
        EXPECT_EQ((*radius)["type"].Get<std::string>(), "length");
        EXPECT_EQ((*radius)["css_variable"].Get<std::string>(), "--radius-card");
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeTest, DistinguishesSetFromUnset) {
    // The whole reason for `is_set`: a field the studio cleared and one they
    // never touched both read as "" from config_secrets, but mean different
    // things to a "reset to default" control.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemeIsSet", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        endpointHelper.GetSecretsHelper()->AddSecret(
            transaction, "site_theme_primary", "#0B6E4F");

        Json::Value body = Json::Value::FromText(
            Call(endpointHelper, crow::HTTPMethod::Get).body);

        const Json::Value* set = Field(body, "theme", "site_theme_primary");
        ASSERT_TRUE(set != nullptr);
        EXPECT_TRUE((*set)["is_set"].Get<bool>());
        EXPECT_EQ((*set)["value"].Get<std::string>(), "#0B6E4F");

        // Untouched tokens have no row at all — the stylesheet holds the default.
        const Json::Value* unset = Field(body, "theme", "site_theme_radius_card");
        ASSERT_TRUE(unset != nullptr);
        EXPECT_FALSE((*unset)["is_set"].Get<bool>());
        EXPECT_EQ((*unset)["value"].Get<std::string>(), "");
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeTest, ShowsWhatTheStudioTypedEvenIfSiteInfoWouldDropIt) {
    // The read path normalizes junk to "" so a page never breaks. The EDITOR
    // must show the real stored value instead, or a studio cannot see and fix
    // the thing that is not working.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemeRaw", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        endpointHelper.GetSecretsHelper()->AddSecret(
            transaction, "site_hero_image_url", "javascript:alert(1)");

        Json::Value body = Json::Value::FromText(
            Call(endpointHelper, crow::HTTPMethod::Get).body);
        const Json::Value* hero = Field(body, "content", "site_hero_image_url");
        ASSERT_TRUE(hero != nullptr);
        EXPECT_EQ((*hero)["value"].Get<std::string>(), "javascript:alert(1)");
        EXPECT_TRUE((*hero)["is_set"].Get<bool>());
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeTest, ListsTheTenantsFontFamiliesForTheRoleDropdowns) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemeFonts", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
        fonts.AddFont(transaction, "Barlow", "sans-serif",
                      DbSchema::kSiteFontSourceKindSystem, 0, "", 10);

        Json::Value body = Json::Value::FromText(
            Call(endpointHelper, crow::HTTPMethod::Get).body);
        const auto& families = body["font_families"].GetArray();
        ASSERT_EQ(families.size(), 1u);
        EXPECT_EQ(families[0]["family"].Get<std::string>(), "Barlow");
        EXPECT_EQ(families[0]["fallback"].Get<std::string>(), "sans-serif");
    });
    Auth::ServerConfig::Shutdown();
}

// ---- PUT ----

TEST(ManageSiteThemeTest, SavesContentAndThemeAndReadsBack) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemePut", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        crow::response resp = Call(
            endpointHelper, crow::HTTPMethod::Put,
            R"({"content":{"site_hero_headline":"Sunrise moves people"},
                "theme":{"site_theme_palette_primary_400":"#0B6E4F"}})");

        EXPECT_EQ(resp.code, 200);
        auto secrets = endpointHelper.GetSecretsHelper();
        EXPECT_EQ(secrets->LookupSecretTest("site_hero_headline"),
                  "Sunrise moves people");
        EXPECT_EQ(secrets->LookupSecretTest("site_theme_palette_primary_400"),
                  "#0B6E4F");
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeTest, WritesOnlyTheKeysPresentSoSectionSavesAreSafe) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemePartial", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        endpointHelper.GetSecretsHelper()->AddSecret(
            transaction, "site_contact_email", "keep@studio.test");

        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Put,
                       R"({"content":{"site_hero_headline":"Only this"}})").code,
                  200);
        // A section-at-a-time save must not blank the sections it did not send.
        EXPECT_EQ(endpointHelper.GetSecretsHelper()->LookupSecretTest(
                      "site_contact_email"), "keep@studio.test");
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeTest, AnEmptyThemeValueClearsTheTokenBackToTheDefault) {
    // "Reset to default" for a token means DELETING the override, and empty is
    // how the editor expresses that — so empty must not be validator-refused.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemeClear", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);
        endpointHelper.GetSecretsHelper()->AddSecret(
            transaction, "site_theme_primary", "#0B6E4F");

        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Put,
                       R"({"theme":{"site_theme_primary":""}})").code, 200);
        EXPECT_EQ(endpointHelper.GetSecretsHelper()->LookupSecretTest(
                      "site_theme_primary"), "");
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeTest, RefusesAnInvalidValueNamingTheField) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemeInvalid", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        // Custom delimiter: the payload contains `)"`, which would otherwise
        // terminate a plain R"( … )" literal early.
        crow::response resp = Call(
            endpointHelper, crow::HTTPMethod::Put,
            R"JSON({"content":{"site_favicon_url":"javascript:alert(1)"}})JSON");
        EXPECT_EQ(resp.code, 400);
        EXPECT_NE(resp.body.find("site_favicon_url"), std::string::npos)
            << resp.body;
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeTest, RefusesTheWholeRequestRatherThanApplyingItHalfway) {
    // A half-saved theme is worse than a refused one: the studio sees part of
    // its change land and has no idea which part failed.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemeAtomic", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        auto secrets = endpointHelper.GetSecretsHelper();
        std::string before = secrets->LookupSecretTest("site_hero_headline");

        // The good field sorts BEFORE the bad one in the registry, so a naive
        // implementation would already have written it.
        crow::response resp = Call(
            endpointHelper, crow::HTTPMethod::Put,
            R"JSON({"content":{"site_hero_headline":"Fine",
                               "site_favicon_url":"javascript:alert(1)"}})JSON");
        EXPECT_EQ(resp.code, 400);
        EXPECT_EQ(secrets->LookupSecretTest("site_hero_headline"), before);
        EXPECT_NE(secrets->LookupSecretTest("site_hero_headline"), "Fine");
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeTest, RefusesAThemeTokenOfTheWrongShape) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemeBadToken", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        crow::response resp = Call(
            endpointHelper, crow::HTTPMethod::Put,
            R"JSON({"theme":{"site_theme_radius_card":"calc(8px + 2px)"}})JSON");
        EXPECT_EQ(resp.code, 400);
        EXPECT_NE(resp.body.find("site_theme_radius_card"), std::string::npos);
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeTest, IgnoresKeysOutsideTheBrandingSurface) {
    // The curated surface must not become a way to write arbitrary secrets —
    // config_secrets holds live credentials.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemeUnknownKey", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        crow::response resp = Call(
            endpointHelper, crow::HTTPMethod::Put,
            R"({"content":{"mail_app_password":"stolen"}})");
        // Nothing recognised → refused, and the credential is untouched.
        EXPECT_EQ(resp.code, 400);
        EXPECT_NE(endpointHelper.GetSecretsHelper()->LookupSecretTest(
                      "mail_app_password"), "stolen");
    });
    Auth::ServerConfig::Shutdown();
}

TEST(ManageSiteThemeTest, NormalizesWindowsLineEndingsOnTheWayIn) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ManageThemeCrLf", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        SignIn(transaction, testDb, endpointHelper, /*makeAdmin=*/true);

        EXPECT_EQ(Call(endpointHelper, crow::HTTPMethod::Put,
                       "{\"content\":{\"site_address_lines\":\"One\\r\\nTwo\"}}").code,
                  200);
        EXPECT_EQ(endpointHelper.GetSecretsHelper()->LookupSecretTest(
                      "site_address_lines"), "One\nTwo");
    });
    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
