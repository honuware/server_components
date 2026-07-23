#include "web_app.h"

#include <memory>

#include <gtest/gtest.h>

#include "endpoints/endpoint_test_helper.h"
#include "business_logic/tenancy/tenant_context.h"
#include "business_logic/tenancy/tenant_resolver.h"
#include "business_logic/tenancy/tenant_resources.h"
#include "test/src/util/database_test_helper.h"

namespace {

TEST(WebAppTenancyTest, ReturnsInstalledResolverAndRegistry) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "WebAppTenancyTest.ReturnsInstalledResolverAndRegistry",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            WebApp& webApp = endpointHelper.GetWebApp();

            // EndpointTestHelper installs a default fixed tenant (Phase 3.4), so
            // both are present after construction.
            EXPECT_NE(webApp.GetTenantResolver(), nullptr);
            EXPECT_NE(webApp.GetTenantResourceRegistry(), nullptr);

            // Installing new ones replaces the defaults; the getters return them.
            Tenancy::TenantContext context;
            context.tenantId = 1;
            context.siteKey = "knotty";
            context.databaseName = "knottyyoga";
            context.status = "active";
            std::shared_ptr<Tenancy::TenantResolver> resolver =
                std::make_shared<Tenancy::FixedTenantResolver>(context);
            std::shared_ptr<Tenancy::TenantResourceRegistry> registry =
                std::make_shared<Tenancy::TenantResourceRegistry>();

            webApp.SetTenantResolver(resolver, Tenancy::TenancyMode::Fixed);
            webApp.SetTenantResourceRegistry(registry);

            EXPECT_EQ(webApp.GetTenantResolver(), resolver);
            EXPECT_EQ(webApp.GetTenantResourceRegistry(), registry);
        });
}

}  // namespace
