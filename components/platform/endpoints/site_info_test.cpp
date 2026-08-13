#include "site_info.h"

#include <string>

#include <gtest/gtest.h>
#include <crow.h>

#include "endpoint_auth_helper.h"
#include "endpoints/endpoint_test_helper.h"
#include "web_app.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/branding/site_content_slots.h"
#include "business_logic/tenancy/tenant_context.h"
#include "db_schema/site_fonts.h"
#include "sql_util/table_helpers/site_fonts.h"
#include "sql_util/database_access/transaction.h"
#include "test/src/util/database_test_helper.h"
#include "util/json_value.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper.h"

namespace Endpoints {
namespace {

// --- BuildSiteInfoResponse: pure JSON shape ---

TEST(SiteInfoTest, BuildSiteInfoResponseMapsAllFields) {
    KeyValueTable content{{"site_hero_headline", "Acme moves people"}};
    KeyValueTable theme{{"--theme-primary", "#0B6E4F"}};
    Json::Value body = BuildSiteInfoResponse(
        "Acme Studio", "https://acme.example", "https://acme.example/logo.svg",
        content, theme, Branding::SiteFontInventory{});
    EXPECT_EQ(body["display_name"].Get<std::string>(), "Acme Studio");
    EXPECT_EQ(body["website_url"].Get<std::string>(), "https://acme.example");
    EXPECT_EQ(body["logo_url"].Get<std::string>(), "https://acme.example/logo.svg");
    EXPECT_EQ(body["content"]["site_hero_headline"].Get<std::string>(),
              "Acme moves people");
    EXPECT_EQ(body["theme"]["--theme-primary"].Get<std::string>(), "#0B6E4F");
}

TEST(SiteInfoTest, BuildSiteInfoResponseEmptyLogoIsEmptyString) {
    // The default: no per-tenant logo set — the SPA falls back to its bundled
    // asset. The key is still present (as an empty string), never omitted.
    Json::Value body = BuildSiteInfoResponse(
        "Acme", "https://acme.example", "", KeyValueTable{}, KeyValueTable{}, Branding::SiteFontInventory{});
    EXPECT_TRUE(body.HasChild("logo_url", nullptr));
    EXPECT_EQ(body["logo_url"].Get<std::string>(), "");
}

TEST(SiteInfoTest, BuildSiteInfoResponseAlwaysEmitsContentAndThemeObjects) {
    // Decision D2: one bootstrap call whose SHAPE never changes. A tenant that
    // has customized nothing still gets both objects, so the SPA's boot applier
    // never has to branch on their absence — and Phase 4 can fill `theme`
    // without touching the client contract.
    Json::Value body = BuildSiteInfoResponse(
        "Acme", "", "", KeyValueTable{}, KeyValueTable{}, Branding::SiteFontInventory{});
    EXPECT_TRUE(body.HasChild("content", nullptr));
    EXPECT_TRUE(body.HasChild("theme", nullptr));
    EXPECT_TRUE(body["content"].GetChildren().empty());
    EXPECT_TRUE(body["theme"].GetChildren().empty());
}

TEST(SiteInfoTest, BuildSiteInfoResponseCarriesTheFontInventory) {
    // Tenant Theming Phase 4B: `fonts` is what the boot initializer needs to
    // LOAD the tenant's families, as distinct from `theme`'s --font-* roles
    // which say which family does which job.
    Branding::SiteFontInventory fonts;
    fonts.preconnects.push_back({"https://fonts.gstatic.com", true});
    fonts.stylesheets.push_back(
        "https://fonts.googleapis.com/css2?family=Barlow&display=swap");
    fonts.faces.push_back(
        {"Studio Sans", 700, "italic", "woff2", "/api/site_font_face/3"});

    Json::Value body = BuildSiteInfoResponse(
        "Acme", "", "", KeyValueTable{}, KeyValueTable{}, fonts);

    const Json::Value& payload = body["fonts"];
    ASSERT_EQ(payload["preconnects"].GetArray().size(), 1u);
    EXPECT_EQ(payload["preconnects"][0]["href"].Get<std::string>(),
              "https://fonts.gstatic.com");
    // crossorigin travels as a real bool, so the client never parses a string.
    EXPECT_TRUE(payload["preconnects"][0]["crossorigin"].Get<bool>());
    ASSERT_EQ(payload["stylesheets"].GetArray().size(), 1u);
    ASSERT_EQ(payload["faces"].GetArray().size(), 1u);
    EXPECT_EQ(payload["faces"][0]["family"].Get<std::string>(), "Studio Sans");
    EXPECT_EQ(payload["faces"][0]["weight"].Get<int64_t>(), 700);
    EXPECT_EQ(payload["faces"][0]["url"].Get<std::string>(),
              "/api/site_font_face/3");
}

TEST(SiteInfoTest, BuildSiteInfoResponseAlwaysEmitsAFontsObject) {
    Json::Value body = BuildSiteInfoResponse(
        "Acme", "", "", KeyValueTable{}, KeyValueTable{},
        Branding::SiteFontInventory{});
    ASSERT_TRUE(body.HasChild("fonts", nullptr));
    EXPECT_TRUE(body["fonts"]["preconnects"].GetArray().empty());
    EXPECT_TRUE(body["fonts"]["stylesheets"].GetArray().empty());
    EXPECT_TRUE(body["fonts"]["faces"].GetArray().empty());
}

TEST(SiteInfoTest, GetSiteInfoComposesFontRoleStacksFromTheNamedFamilysRow) {
    // D13: the role token names a FAMILY; the stack comes from that family's
    // row, so the fallback is defined in exactly one place.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SiteInfoFontRole", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
        fonts.AddFont(transaction, "Barlow", "sans-serif",
                      DbSchema::kSiteFontSourceKindSystem, 0, "", 10);

        crow::request req;
        req.method = crow::HTTPMethod::Get;
        crow::response resp;
        EndpointAuthHelper helper(endpointHelper.GetWebApp(), req, resp);
        helper.Initialize();

        helper.GetTransactionProvider()->RunInTransaction([&](Transaction& t) {
            Secrets::SecretsHelperPtr secrets = helper.GetSecretsHelper();
            secrets->AddSecret(t, "site_theme_font_heading", "Barlow");
            // A role naming a family with no row keeps the stylesheet default
            // rather than emitting a bare, unloadable family name.
            secrets->AddSecret(t, "site_theme_font_body", "Nonexistent");
        });

        Json::Value body = GetSiteInfo(helper);
        const Json::Value& theme = body["theme"];
        EXPECT_EQ(theme["--font-heading"].Get<std::string>(), "'Barlow', sans-serif");
        EXPECT_FALSE(theme.HasChild("--font-body", nullptr));
    });

    Auth::ServerConfig::Shutdown();
}

TEST(SiteInfoTest, BuildSiteInfoResponseKeepsNumericLookingCopyAsAString) {
    // A studio whose browser title is "2026" must not come back as a number —
    // the reason this builder does not route through KeyValueTableToJson.
    KeyValueTable content{{"site_browser_title", "2026"}};
    Json::Value body = BuildSiteInfoResponse(
        "Acme", "", "", content, KeyValueTable{}, Branding::SiteFontInventory{});
    EXPECT_EQ(body["content"]["site_browser_title"].Get<std::string>(), "2026");
}

// --- GetSiteInfo: sources branding from the resolved tenant's secrets ---

TEST(SiteInfoTest, GetSiteInfoReturnsBrandingFromTenantSecrets) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "SiteInfoBrandingFromSecrets", [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);

            crow::request req;
            req.method = crow::HTTPMethod::Get;
            crow::response resp;
            EndpointAuthHelper helper(endpointHelper.GetWebApp(), req, resp);
            helper.Initialize();

            // Set known branding on the resolved tenant's secrets (the in-memory
            // test double), so the assertion carries no brand literal from the
            // framework and proves the endpoint sources each field distinctly.
            helper.GetTransactionProvider()->RunInTransaction(
                [&](Transaction& t) {
                    Secrets::SecretsHelperPtr secrets = helper.GetSecretsHelper();
                    secrets->AddSecret(t, Secrets::kMailSenderName, "Test Studio");
                    secrets->AddSecret(
                        t, Secrets::kWebsiteAddressLogin, "https://test.example");
                    secrets->AddSecret(
                        t, Secrets::kSiteLogoUrl, "https://test.example/logo.svg");
                });

            Json::Value body = GetSiteInfo(helper);
            EXPECT_EQ(body["display_name"].Get<std::string>(), "Test Studio");
            EXPECT_EQ(body["website_url"].Get<std::string>(),
                      "https://test.example");
            EXPECT_EQ(body["logo_url"].Get<std::string>(),
                      "https://test.example/logo.svg");
        });

    Auth::ServerConfig::Shutdown();
}

TEST(SiteInfoTest, GetSiteInfoCarriesTheTenantsContentSlots) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "SiteInfoContentSlots", [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);

            crow::request req;
            req.method = crow::HTTPMethod::Get;
            crow::response resp;
            EndpointAuthHelper helper(endpointHelper.GetWebApp(), req, resp);
            helper.Initialize();

            helper.GetTransactionProvider()->RunInTransaction(
                [&](Transaction& t) {
                    Secrets::SecretsHelperPtr secrets = helper.GetSecretsHelper();
                    secrets->AddSecret(
                        t, Secrets::kSiteHeroHeadline, "Acme moves people");
                    // Junk in the store must not reach the client (D10).
                    secrets->AddSecret(
                        t, Secrets::kSiteHeroImageUrl, "javascript:alert(1)");
                });

            Json::Value body = GetSiteInfo(helper);
            const Json::Value& content = body["content"];
            EXPECT_EQ(content["site_hero_headline"].Get<std::string>(),
                      "Acme moves people");
            EXPECT_EQ(content["site_hero_image_url"].Get<std::string>(), "");
            // Every registered slot travels, so the SPA's merge is total.
            EXPECT_EQ(content.GetChildren().size(),
                      Branding::SiteContentSlots().size());
        });

    Auth::ServerConfig::Shutdown();
}

TEST(SiteInfoTest, GetSiteInfoFallsBackToContextDisplayNameWhenStudioNameBlank) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "SiteInfoDisplayNameFallback", [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);

            crow::request req;
            req.method = crow::HTTPMethod::Get;
            crow::response resp;
            EndpointAuthHelper helper(endpointHelper.GetWebApp(), req, resp);
            helper.Initialize();

            // Blank studio name → display_name falls back to the tenants-row
            // display name (whatever the resolver populated). Asserting the
            // fallback WIRING (display_name tracks the context) is value-agnostic.
            helper.GetTransactionProvider()->RunInTransaction(
                [&](Transaction& t) {
                    helper.GetSecretsHelper()->AddSecret(
                        t, Secrets::kMailSenderName, "");
                });

            Json::Value body = GetSiteInfo(helper);
            EXPECT_EQ(body["display_name"].Get<std::string>(),
                      helper.GetTenantContext().displayName);
        });

    Auth::ServerConfig::Shutdown();
}

TEST(SiteInfoTest, GetSiteInfoCarriesOnlyTheThemeTokensATenantOverrode) {
    // Tenant Theming Phase 4: the SPA's stylesheet holds every default, so the
    // payload is the OVERRIDE layer. An unset token must be absent rather than
    // empty — an empty custom property is invalid at computed-value time and
    // would break the very cascade the defaults provide.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SiteInfoTheme", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        crow::request req;
        req.method = crow::HTTPMethod::Get;
        crow::response resp;
        EndpointAuthHelper helper(endpointHelper.GetWebApp(), req, resp);
        helper.Initialize();

        helper.GetTransactionProvider()->RunInTransaction([&](Transaction& t) {
            Secrets::SecretsHelperPtr secrets = helper.GetSecretsHelper();
            secrets->AddSecret(t, "site_theme_palette_primary_400", "#0B6E4F");
            secrets->AddSecret(t, "site_theme_radius_card", "2px");
            // Junk must not reach the client (D10).
            secrets->AddSecret(t, "site_theme_accent", "chartreuse");
        });

        Json::Value body = GetSiteInfo(helper);
        const Json::Value& theme = body["theme"];
        EXPECT_EQ(theme["--palette-primary-400"].Get<std::string>(), "#0B6E4F");
        EXPECT_EQ(theme["--radius-card"].Get<std::string>(), "2px");
        EXPECT_FALSE(theme.HasChild("--theme-accent", nullptr));
        EXPECT_EQ(theme.GetChildren().size(), 2u);
    });

    Auth::ServerConfig::Shutdown();
}

// --- HTTP integration (full route via Crow) ---

TEST(SiteInfoTest, HttpEndpointReturnsBrandingUnauthenticated) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SiteInfoHttp", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        crow::request req;
        req.method = crow::HTTPMethod::Get;
        req.url = "/api/site_info";
        // Deliberately no auth cookie — the endpoint is unauthenticated.
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);
        EXPECT_EQ(resp.get_header_value("Content-Type"), "application/json");
        EXPECT_EQ(resp.get_header_value("Cache-Control"), "public, max-age=300");

        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_TRUE(body.HasChild("display_name", nullptr));
        EXPECT_TRUE(body.HasChild("website_url", nullptr));
        EXPECT_TRUE(body.HasChild("logo_url", nullptr));
        EXPECT_TRUE(body.HasChild("content", nullptr));
        EXPECT_TRUE(body.HasChild("theme", nullptr));
        EXPECT_TRUE(body["content"].HasChild("site_about_markdown", nullptr));
    });

    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
