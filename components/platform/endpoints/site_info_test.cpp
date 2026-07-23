#include "site_info.h"

#include <string>

#include <gtest/gtest.h>
#include <crow.h>

#include "endpoint_auth_helper.h"
#include "endpoints/endpoint_test_helper.h"
#include "web_app.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/tenancy/tenant_context.h"
#include "sql_util/database_access/transaction.h"
#include "test/src/util/database_test_helper.h"
#include "util/json_value.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper.h"

namespace Endpoints {
namespace {

// --- BuildSiteInfoResponse: pure JSON shape ---

TEST(SiteInfoTest, BuildSiteInfoResponseMapsAllFields) {
    Json::Value body = BuildSiteInfoResponse(
        "Acme Studio", "https://acme.example", "https://acme.example/logo.svg");
    EXPECT_EQ(body["display_name"].Get<std::string>(), "Acme Studio");
    EXPECT_EQ(body["website_url"].Get<std::string>(), "https://acme.example");
    EXPECT_EQ(body["logo_url"].Get<std::string>(), "https://acme.example/logo.svg");
}

TEST(SiteInfoTest, BuildSiteInfoResponseEmptyLogoIsEmptyString) {
    // The default: no per-tenant logo set — the SPA falls back to its bundled
    // asset. The key is still present (as an empty string), never omitted.
    Json::Value body = BuildSiteInfoResponse("Acme", "https://acme.example", "");
    EXPECT_TRUE(body.HasChild("logo_url", nullptr));
    EXPECT_EQ(body["logo_url"].Get<std::string>(), "");
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
    });

    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
