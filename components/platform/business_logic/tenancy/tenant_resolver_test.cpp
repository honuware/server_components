#include "tenant_resolver.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "tenant_context.h"
#include "db_schema/tenants.h"
#include "sql_util/database_access/transaction_provider.h"
#include "sql_util/table_helpers/tenants.h"
#include "test/src/util/database_test_helper.h"

namespace Tenancy {
namespace {

// Wraps the test's transaction (so seeded rows are visible) while counting how
// many times a component opens a "transaction" — used to prove the resolver
// caches and only re-queries after Invalidate.
class CountingTransactionProvider : public TransactionProvider {
public:
    explicit CountingTransactionProvider(Transaction& transaction)
        : transaction_(transaction) {}

    void RunInTransaction(TransactionProvider::DatabaseFunc databaseFunc) override {
        ++count_;
        databaseFunc(transaction_);
    }

    int count() const { return count_; }

private:
    Transaction& transaction_;
    int count_ = 0;
};

TableHelpers::TenantRow MakeRow(
    std::string_view siteKey, std::string_view databaseName,
    std::string_view displayName) {
    TableHelpers::TenantRow row;
    row.siteKey = std::string(siteKey);
    row.databaseName = std::string(databaseName);
    row.displayName = std::string(displayName);
    return row;
}

TEST(TenantResolverTest, ControlDbResolvesAndCaches) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("TenantResolverTest.ControlDbResolvesAndCaches",
        [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            TableHelpers::Tenants tenants(databaseHelper);
            tenants.Insert(transaction, MakeRow("site-a", "honuware_a", "Site A"));

            auto counting =
                std::make_shared<CountingTransactionProvider>(transaction);
            ControlDbTenantResolver resolver(counting, databaseHelper);

            std::optional<TenantContext> first = resolver.Resolve("site-a");
            ASSERT_TRUE(first.has_value());
            EXPECT_EQ(first->siteKey, "site-a");
            EXPECT_EQ(first->databaseName, "honuware_a");
            EXPECT_EQ(first->displayName, "Site A");
            EXPECT_EQ(first->status, std::string(DbSchema::kTenantStatusActive));
            EXPECT_EQ(first->maxConnections, 1);
            EXPECT_EQ(counting->count(), 1);

            // Second resolve is served from cache — no re-query.
            std::optional<TenantContext> second = resolver.Resolve("site-a");
            ASSERT_TRUE(second.has_value());
            EXPECT_EQ(counting->count(), 1);

            // Invalidate forces a re-query.
            resolver.Invalidate("site-a");
            std::optional<TenantContext> third = resolver.Resolve("site-a");
            ASSERT_TRUE(third.has_value());
            EXPECT_EQ(counting->count(), 2);
        });
}

TEST(TenantResolverTest, ControlDbUnknownSiteReturnsEmpty) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("TenantResolverTest.ControlDbUnknownSiteReturnsEmpty",
        [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            auto provider =
                std::make_shared<CountingTransactionProvider>(transaction);
            ControlDbTenantResolver resolver(provider, databaseHelper);
            EXPECT_FALSE(resolver.Resolve("does-not-exist").has_value());
        });
}

TEST(TenantResolverTest, ControlDbSuspendedResolvesWithStatus) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("TenantResolverTest.ControlDbSuspendedResolvesWithStatus",
        [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            TableHelpers::Tenants tenants(databaseHelper);
            tenants.Insert(transaction, MakeRow("site-b", "honuware_b", "Site B"));
            tenants.SetStatus(transaction, "site-b", DbSchema::kTenantStatusSuspended);

            auto provider =
                std::make_shared<CountingTransactionProvider>(transaction);
            ControlDbTenantResolver resolver(provider, databaseHelper);

            std::optional<TenantContext> resolved = resolver.Resolve("site-b");
            ASSERT_TRUE(resolved.has_value());
            EXPECT_EQ(resolved->status,
                      std::string(DbSchema::kTenantStatusSuspended));
        });
}

TEST(TenantResolverTest, FixedResolverServesEmptyAndOwnKeyRejectsForeign) {
    TenantContext context;
    context.tenantId = 1;
    context.siteKey = "knotty";
    context.databaseName = "knottyyoga";
    context.displayName = "Knotty Yoga";
    context.status = std::string(DbSchema::kTenantStatusActive);
    context.maxConnections = 1;
    FixedTenantResolver resolver(context);

    // Empty header (dev / single-tenant, no CloudFront) → the fixed tenant.
    std::optional<TenantContext> empty = resolver.Resolve("");
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(empty->siteKey, "knotty");

    // Its own site key → the fixed tenant.
    std::optional<TenantContext> own = resolver.Resolve("knotty");
    ASSERT_TRUE(own.has_value());
    EXPECT_EQ(own->databaseName, "knottyyoga");

    // A contradicting site key → empty, so the edge can reject it.
    EXPECT_FALSE(resolver.Resolve("some-other-site").has_value());

    // Invalidate is a no-op; Resolve keeps working.
    resolver.Invalidate("knotty");
    resolver.InvalidateAll();
    EXPECT_TRUE(resolver.Resolve("knotty").has_value());
}

}  // namespace
}  // namespace Tenancy
