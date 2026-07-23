#include "endpoint_auth_helper.h"

#include <string>

#include <gtest/gtest.h>
#include <crow.h>

#include "endpoints/endpoint_test_helper.h"
#include "web_app.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/tenancy/tenant_context.h"
#include "business_logic/tenancy/tenant_header.h"
#include "business_logic/tenancy/tenant_resources.h"
#include "test/src/util/database_test_helper.h"
#include "util/json_value.h"
#include "util/types.h"

namespace Endpoints {
namespace {

// Test-only route that resolves the request's tenant through EndpointAuthHelper
// and echoes what the accessors return. Registered globally (test binaries only —
// _test.cpp never links into the production core), so tenancy tests can observe
// that Initialize() re-pointed the provider/database to the resolved tenant.
class TenantEchoRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/test_tenant_echo")
            .methods(crow::HTTPMethod::Get)(
                [=](const crow::request& req, crow::response& resp) {
                    EndpointAuthHelper helper(*webApp, req, resp);
                    helper.Initialize();
                    const Tenancy::TenantContext& ctx = helper.GetTenantContext();
                    const bool hasResources = helper.GetTenantResources() != nullptr;
                    const bool providerIsTenant =
                        hasResources &&
                        helper.GetTransactionProvider() ==
                            helper.GetTenantResources()->transactionProvider;
                    // Phase 4.1: GetSecretsHelper() re-points to the tenant's.
                    const bool secretsIsTenant =
                        hasResources &&
                        helper.GetTenantResources()->secretsHelper != nullptr &&
                        helper.GetSecretsHelper() ==
                            helper.GetTenantResources()->secretsHelper;
                    // Phase 4.3: GetMailHelper() re-points to the tenant's.
                    const bool mailIsTenant =
                        hasResources &&
                        helper.GetTenantResources()->mailHelper != nullptr &&
                        helper.GetMailHelper() ==
                            helper.GetTenantResources()->mailHelper;
                    Json::Value body(Json::JsonObject{
                        {"tenant_id", Json::Value(StringFromInt(ctx.tenantId))},
                        {"site_key", Json::Value(ctx.siteKey)},
                        {"database_name", Json::Value(ctx.databaseName)},
                        {"db_helper_name",
                         Json::Value(helper.GetDatabaseHelper().GetDatabaseName())},
                        {"has_resources", Json::Value(hasResources)},
                        {"provider_is_tenant", Json::Value(providerIsTenant)},
                        {"secrets_is_tenant", Json::Value(secretsIsTenant)},
                        {"mail_is_tenant", Json::Value(mailIsTenant)},
                    });
                    resp.code = 200;
                    resp.set_header("Content-Type", "application/json");
                    resp.write(body.ToString());
                    resp.end();
                });
    }
} g_tenantEchoRouting;

Json::Value CallEcho(EndpointTestHelper& endpointHelper, const std::string& siteKey) {
    crow::request req;
    req.method = crow::HTTPMethod::Get;
    req.url = "/api/test_tenant_echo";
    if (!siteKey.empty()) {
        req.add_header(std::string(Tenancy::kSiteHeaderName), siteKey);
    }
    crow::response resp;
    endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
    EXPECT_EQ(resp.code, 200);
    return Json::Value::FromText(resp.body);
}

TEST(EndpointAuthHelperTenancyTest, DefaultFixedTenantResolvedAndRepointed) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "DefaultFixedTenantResolvedAndRepointed", [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            const std::string testDbName =
                testDb.GetDatabaseHelper().GetDatabaseName();

            // No site header — the default fixed tenant admits via its empty-key
            // path and EndpointAuthHelper routes to the test-database resources.
            Json::Value body = CallEcho(endpointHelper, "");

            EXPECT_TRUE(body["has_resources"].Get<bool>());
            EXPECT_EQ(body["site_key"].Get<std::string>(),
                      std::string(EndpointTestHelper::kDefaultTestSiteKey));
            EXPECT_EQ(body["tenant_id"].Get<std::string>(), "1");
            EXPECT_EQ(body["database_name"].Get<std::string>(), testDbName);
            // GetDatabaseHelper() / GetTransactionProvider() re-pointed to tenant.
            EXPECT_EQ(body["db_helper_name"].Get<std::string>(), testDbName);
            EXPECT_TRUE(body["provider_is_tenant"].Get<bool>());
            // Phase 4.1/4.3: GetSecretsHelper()/GetMailHelper() re-pointed to the
            // tenant's (the test doubles injected into the default fixed tenant).
            EXPECT_TRUE(body["secrets_is_tenant"].Get<bool>());
            EXPECT_TRUE(body["mail_is_tenant"].Get<bool>());
        });

    Auth::ServerConfig::Shutdown();
}

TEST(EndpointAuthHelperTenancyTest, MatchingFixedHeaderResolves) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "MatchingFixedHeaderResolves", [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);

            Json::Value body = CallEcho(
                endpointHelper,
                std::string(EndpointTestHelper::kDefaultTestSiteKey));

            EXPECT_TRUE(body["has_resources"].Get<bool>());
            EXPECT_EQ(body["site_key"].Get<std::string>(),
                      std::string(EndpointTestHelper::kDefaultTestSiteKey));
        });

    Auth::ServerConfig::Shutdown();
}

TEST(EndpointAuthHelperTenancyTest, ControlModeRoutesDistinctTenants) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ControlModeRoutesDistinctTenants", [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            EndpointTestHelper::ControlTenants tenants =
                endpointHelper.InstallControlModeTenants(
                    transaction, "site-alpha", "site-beta");

            ASSERT_NE(tenants.a.tenantId, 0);
            ASSERT_NE(tenants.b.tenantId, 0);
            ASSERT_NE(tenants.a.tenantId, tenants.b.tenantId);

            Json::Value alpha = CallEcho(endpointHelper, "site-alpha");
            Json::Value beta = CallEcho(endpointHelper, "site-beta");

            EXPECT_EQ(alpha["site_key"].Get<std::string>(), "site-alpha");
            EXPECT_EQ(alpha["tenant_id"].Get<std::string>(),
                      StringFromInt(tenants.a.tenantId));
            EXPECT_EQ(beta["site_key"].Get<std::string>(), "site-beta");
            EXPECT_EQ(beta["tenant_id"].Get<std::string>(),
                      StringFromInt(tenants.b.tenantId));

            // Distinct tenants -> distinct resolved resources.
            EXPECT_NE(alpha["tenant_id"].Get<std::string>(),
                      beta["tenant_id"].Get<std::string>());
        });

    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
