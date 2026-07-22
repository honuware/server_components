#include "tenants.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "db_schema/tenants.h"
#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"

namespace TableHelpers {
namespace {

TenantRow MakeRow(
    std::string_view siteKey, std::string_view databaseName,
    std::string_view displayName) {
    TenantRow row;
    row.siteKey = std::string(siteKey);
    row.databaseName = std::string(databaseName);
    row.displayName = std::string(displayName);
    return row;
}

bool HasSiteKey(const std::vector<TenantRow>& rows, std::string_view siteKey) {
    for (const TenantRow& row : rows) {
        if (row.siteKey == siteKey) return true;
    }
    return false;
}

TEST(TenantsTest, InsertAndLookupRoundTrip) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("TenantsTest.InsertAndLookupRoundTrip",
        [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            Tenants tenants(databaseHelper);

            int64_t id = tenants.Insert(
                transaction, MakeRow("acme", "honuware_acme", "Acme Studio"));
            EXPECT_GT(id, 0);

            std::optional<TenantRow> row =
                tenants.LookupBySiteKey(transaction, "acme");
            ASSERT_TRUE(row.has_value());
            EXPECT_EQ(row->id, id);
            EXPECT_EQ(row->siteKey, "acme");
            EXPECT_EQ(row->databaseName, "honuware_acme");
            EXPECT_EQ(row->displayName, "Acme Studio");
            // Column defaults applied.
            EXPECT_EQ(row->status, std::string(DbSchema::kTenantStatusActive));
            EXPECT_EQ(row->maxConnections, 1);
        });
}

TEST(TenantsTest, LookupUnknownSiteKeyReturnsEmpty) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("TenantsTest.LookupUnknownSiteKeyReturnsEmpty",
        [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            Tenants tenants(databaseHelper);
            EXPECT_FALSE(tenants.LookupBySiteKey(transaction, "nope").has_value());
        });
}

TEST(TenantsTest, DuplicateSiteKeyThrows) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("TenantsTest.DuplicateSiteKeyThrows",
        [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            Tenants tenants(databaseHelper);
            tenants.Insert(transaction, MakeRow("dup", "honuware_dup1", "One"));
            EXPECT_ANY_THROW(
                tenants.Insert(transaction, MakeRow("dup", "honuware_dup2", "Two")));
        });
}

TEST(TenantsTest, DuplicateDatabaseNameThrows) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("TenantsTest.DuplicateDatabaseNameThrows",
        [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            Tenants tenants(databaseHelper);
            tenants.Insert(transaction, MakeRow("one", "honuware_shared", "One"));
            EXPECT_ANY_THROW(
                tenants.Insert(transaction, MakeRow("two", "honuware_shared", "Two")));
        });
}

TEST(TenantsTest, ListActiveFiltersSuspended) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("TenantsTest.ListActiveFiltersSuspended",
        [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            Tenants tenants(databaseHelper);
            tenants.Insert(transaction, MakeRow("active-a", "honuware_a", "A"));
            tenants.Insert(transaction, MakeRow("suspended-b", "honuware_b", "B"));
            tenants.SetStatus(
                transaction, "suspended-b", DbSchema::kTenantStatusSuspended);

            std::vector<TenantRow> active = tenants.ListActive(transaction);
            EXPECT_TRUE(HasSiteKey(active, "active-a"));
            EXPECT_FALSE(HasSiteKey(active, "suspended-b"));
        });
}

TEST(TenantsTest, SetStatusTransitions) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("TenantsTest.SetStatusTransitions",
        [&](Transaction& transaction) {
            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            Tenants tenants(databaseHelper);
            tenants.Insert(transaction, MakeRow("t", "honuware_t", "T"));
            ASSERT_TRUE(tenants.LookupBySiteKey(transaction, "t").has_value());
            EXPECT_EQ(tenants.LookupBySiteKey(transaction, "t")->status,
                      std::string(DbSchema::kTenantStatusActive));

            tenants.SetStatus(transaction, "t", DbSchema::kTenantStatusSuspended);
            EXPECT_EQ(tenants.LookupBySiteKey(transaction, "t")->status,
                      std::string(DbSchema::kTenantStatusSuspended));
        });
}

}  // namespace
}  // namespace TableHelpers
