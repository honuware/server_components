#include "tenant_resources.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "tenant_context.h"
#include "sql_util/database_access/pooled_transaction_provider.h"
#include "sql_util/database_access/transaction.h"
#include "test/src/util/database_test_helper.h"

namespace Tenancy {
namespace {

TenantContext MakeContext(
    int64_t tenantId, std::string_view siteKey,
    std::string_view databaseName, int64_t maxConnections = 1) {
    TenantContext context;
    context.tenantId = tenantId;
    context.siteKey = std::string(siteKey);
    context.databaseName = std::string(databaseName);
    context.status = "active";
    context.maxConnections = maxConnections;
    return context;
}

TEST(TenantResourcesTest, TenantTransactionProviderBoundToDatabaseAndMax) {
    // No connection is opened here — the pool is lazy — so a made-up database name
    // is fine; we only assert the provider is configured for it.
    TenantContext context = MakeContext(1, "acme", "honuware_acme", /*max=*/3);
    TransactionProviderPtr provider = MakeTenantTransactionProvider(context);

    auto pool = std::dynamic_pointer_cast<PooledTransactionProvider>(provider);
    ASSERT_NE(pool, nullptr);
    EXPECT_EQ(pool->GetDatabaseName(), "honuware_acme");
    EXPECT_EQ(pool->GetMaxConnections(), 3);
    EXPECT_EQ(pool->GetCreatedConnectionCount(), 0);  // still lazy
}

TEST(TenantResourcesTest, RegistryBuildsOnceAndCaches) {
    int factoryCalls = 0;
    TenantResourceRegistry registry([&](const TenantContext& context) {
        ++factoryCalls;
        auto resources = std::make_shared<TenantResources>();
        resources->context = context;
        return resources;
    });

    TenantContext context = MakeContext(7, "acme", "honuware_acme");
    TenantResourcesPtr first = registry.GetOrCreate(context);
    TenantResourcesPtr second = registry.GetOrCreate(context);

    EXPECT_EQ(first, second);       // same cached pointer
    EXPECT_EQ(factoryCalls, 1);     // built only once
    EXPECT_EQ(first->context.tenantId, 7);
}

TEST(TenantResourcesTest, RegistryDistinctTenantsGetDistinctResources) {
    TenantResourceRegistry registry([](const TenantContext& context) {
        auto resources = std::make_shared<TenantResources>();
        resources->context = context;
        return resources;
    });

    TenantResourcesPtr a = registry.GetOrCreate(MakeContext(1, "a", "honuware_a"));
    TenantResourcesPtr b = registry.GetOrCreate(MakeContext(2, "b", "honuware_b"));

    EXPECT_NE(a, b);
    EXPECT_EQ(a->context.databaseName, "honuware_a");
    EXPECT_EQ(b->context.databaseName, "honuware_b");
}

// An app-derived resources type carrying an app-specific field (mirrors the
// Phase 4.2 KnottyTenantResources adding a SquareClient).
struct DerivedTenantResources : TenantResources {
    int appMarker = 42;
};

TEST(TenantResourcesTest, RegistryCustomFactoryYieldsDerivedType) {
    TenantResourceRegistry registry([](const TenantContext& context) {
        auto resources = std::make_shared<DerivedTenantResources>();
        resources->context = context;
        return resources;  // upcast to TenantResourcesPtr
    });

    TenantResourcesPtr resources =
        registry.GetOrCreate(MakeContext(1, "a", "honuware_a"));
    auto derived = std::dynamic_pointer_cast<DerivedTenantResources>(resources);
    ASSERT_NE(derived, nullptr);
    EXPECT_EQ(derived->appMarker, 42);
}

TEST(TenantResourcesTest, RegistryEvictRebuildsOnNextGet) {
    int factoryCalls = 0;
    TenantResourceRegistry registry([&](const TenantContext& context) {
        ++factoryCalls;
        auto resources = std::make_shared<TenantResources>();
        resources->context = context;
        return resources;
    });

    TenantContext context = MakeContext(3, "c", "honuware_c");
    TenantResourcesPtr first = registry.GetOrCreate(context);
    registry.Evict(3);
    TenantResourcesPtr second = registry.GetOrCreate(context);

    EXPECT_NE(first, second);    // rebuilt after eviction
    EXPECT_EQ(factoryCalls, 2);
}

TEST(TenantResourcesTest, DefaultFactoryBuildsUsableResourcesAgainstRealDb) {
    // The default framework factory opens a real connection, so point it at the
    // active test database.
    TestDatabaseUtil testDb;
    std::string dbName = testDb.GetDatabaseHelper().GetDatabaseName();
    TenantContext context = MakeContext(1, "test", dbName);

    TenantResourcesPtr resources = MakeDefaultTenantResources(context);
    ASSERT_NE(resources, nullptr);
    EXPECT_EQ(resources->context.databaseName, dbName);
    ASSERT_NE(resources->transactionProvider, nullptr);

    // The provider is usable end to end.
    bool ran = false;
    resources->transactionProvider->RunInTransaction(
        [&](Transaction&) { ran = true; });
    EXPECT_TRUE(ran);
}

}  // namespace
}  // namespace Tenancy
