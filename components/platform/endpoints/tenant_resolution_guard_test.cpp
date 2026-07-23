#include "tenant_resolution_guard.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <crow.h>

#include "business_logic/tenancy/tenant_context.h"
#include "business_logic/tenancy/tenant_header.h"
#include "business_logic/tenancy/tenant_resolver.h"
#include "db_schema/tenants.h"
#include "sql_util/database_access/transaction_provider.h"
#include "sql_util/table_helpers/tenants.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/test_transaction_provider.h"
#include "util/json_value.h"

namespace Endpoints {
namespace {

crow::request MakeReq(const std::string& url) {
    crow::request req;
    req.url = url;
    return req;
}

void SetSite(crow::request& req, const std::string& siteKey) {
    req.add_header(std::string(Tenancy::kSiteHeaderName), siteKey);
}

Tenancy::TenantContext FixedContext() {
    Tenancy::TenantContext context;
    context.tenantId = 1;
    context.siteKey = "the-site";
    context.databaseName = "the_db";
    context.status = std::string(DbSchema::kTenantStatusActive);
    context.maxConnections = 1;
    return context;
}

std::string ErrorOf(const crow::response& res) {
    Json::Value body = Json::Value::FromText(res.body);
    return body["error"].Get<std::string>();
}

// --- Inert (no resolver installed) ---

TEST(TenantResolutionGuardTest, InertWithoutResolver) {
    TenantResolutionGuard guard;  // SetResolver never called

    crow::request req = MakeReq("/api/me");
    SetSite(req, "anything");
    crow::response res;
    TenantResolutionGuard::context ctx;
    guard.before_handle(req, res, ctx);

    EXPECT_FALSE(res.is_completed());
    EXPECT_FALSE(ctx.resolved);
}

// --- Fixed mode ---

TEST(TenantResolutionGuardTest, FixedAdmitsHeaderlessRequest) {
    TenantResolutionGuard guard;
    guard.SetResolver(
        std::make_shared<Tenancy::FixedTenantResolver>(FixedContext()),
        Tenancy::TenancyMode::Fixed);

    crow::request req = MakeReq("/api/me");  // no site header
    crow::response res;
    TenantResolutionGuard::context ctx;
    guard.before_handle(req, res, ctx);

    EXPECT_FALSE(res.is_completed());
    ASSERT_TRUE(ctx.resolved);
    EXPECT_EQ(ctx.tenant.siteKey, "the-site");
    EXPECT_EQ(ctx.tenant.databaseName, "the_db");
}

TEST(TenantResolutionGuardTest, FixedAdmitsMatchingHeader) {
    TenantResolutionGuard guard;
    guard.SetResolver(
        std::make_shared<Tenancy::FixedTenantResolver>(FixedContext()),
        Tenancy::TenancyMode::Fixed);

    crow::request req = MakeReq("/api/me");
    SetSite(req, "the-site");
    crow::response res;
    TenantResolutionGuard::context ctx;
    guard.before_handle(req, res, ctx);

    EXPECT_FALSE(res.is_completed());
    ASSERT_TRUE(ctx.resolved);
    EXPECT_EQ(ctx.tenant.siteKey, "the-site");
}

TEST(TenantResolutionGuardTest, FixedRejectsContradictingHeader) {
    TenantResolutionGuard guard;
    guard.SetResolver(
        std::make_shared<Tenancy::FixedTenantResolver>(FixedContext()),
        Tenancy::TenancyMode::Fixed);

    crow::request req = MakeReq("/api/me");
    SetSite(req, "some-other-site");
    crow::response res;
    TenantResolutionGuard::context ctx;
    guard.before_handle(req, res, ctx);

    EXPECT_TRUE(res.is_completed());
    EXPECT_EQ(res.code, 400);
    EXPECT_FALSE(ctx.resolved);
    EXPECT_EQ(ErrorOf(res), "site_mismatch");
}

TEST(TenantResolutionGuardTest, FixedAllowsHealthEvenWithForeignHeader) {
    TenantResolutionGuard guard;
    guard.SetResolver(
        std::make_shared<Tenancy::FixedTenantResolver>(FixedContext()),
        Tenancy::TenancyMode::Fixed);

    // Health probes are allow-listed regardless of any (even contradicting) header.
    crow::request req = MakeReq("/api/health");
    SetSite(req, "some-other-site");
    crow::response res;
    TenantResolutionGuard::context ctx;
    guard.before_handle(req, res, ctx);

    EXPECT_FALSE(res.is_completed());
    EXPECT_FALSE(ctx.resolved);  // allow-listed: guard returns before resolving
}

// --- Control mode (real control rows over the test database) ---

TEST(TenantResolutionGuardTest, ControlMissingHeaderRejected) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ControlMissingHeaderRejected", [&](Transaction& transaction) {
            TransactionProviderPtr provider = MakeTestTransactionProvider(transaction);
            DatabaseHelper db = testDb.GetDatabaseHelper();

            TenantResolutionGuard guard;
            guard.SetResolver(
                std::make_shared<Tenancy::ControlDbTenantResolver>(provider, db),
                Tenancy::TenancyMode::Control);

            crow::request req = MakeReq("/api/me");  // no site header
            crow::response res;
            TenantResolutionGuard::context ctx;
            guard.before_handle(req, res, ctx);

            EXPECT_TRUE(res.is_completed());
            EXPECT_EQ(res.code, 421);
            EXPECT_FALSE(ctx.resolved);
            EXPECT_EQ(ErrorOf(res), "missing_site_header");
        });
}

TEST(TenantResolutionGuardTest, ControlUnknownSiteRejected) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ControlUnknownSiteRejected", [&](Transaction& transaction) {
            TransactionProviderPtr provider = MakeTestTransactionProvider(transaction);
            DatabaseHelper db = testDb.GetDatabaseHelper();

            TenantResolutionGuard guard;
            guard.SetResolver(
                std::make_shared<Tenancy::ControlDbTenantResolver>(provider, db),
                Tenancy::TenancyMode::Control);

            crow::request req = MakeReq("/api/me");
            SetSite(req, "no-such-site");
            crow::response res;
            TenantResolutionGuard::context ctx;
            guard.before_handle(req, res, ctx);

            EXPECT_TRUE(res.is_completed());
            EXPECT_EQ(res.code, 421);
            EXPECT_FALSE(ctx.resolved);
            EXPECT_EQ(ErrorOf(res), "unknown_site");
        });
}

TEST(TenantResolutionGuardTest, ControlKnownSiteAdmitted) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ControlKnownSiteAdmitted", [&](Transaction& transaction) {
            DatabaseHelper db = testDb.GetDatabaseHelper();
            TableHelpers::Tenants tenants(db);
            TableHelpers::TenantRow row;
            row.siteKey = "site-active";
            row.databaseName = db.GetDatabaseName();
            row.displayName = "Active Site";
            row.status = std::string(DbSchema::kTenantStatusActive);
            row.maxConnections = 1;
            const int64_t tenantId = tenants.Insert(transaction, row);

            TransactionProviderPtr provider = MakeTestTransactionProvider(transaction);
            TenantResolutionGuard guard;
            guard.SetResolver(
                std::make_shared<Tenancy::ControlDbTenantResolver>(provider, db),
                Tenancy::TenancyMode::Control);

            crow::request req = MakeReq("/api/me");
            SetSite(req, "site-active");
            crow::response res;
            TenantResolutionGuard::context ctx;
            guard.before_handle(req, res, ctx);

            EXPECT_FALSE(res.is_completed());
            ASSERT_TRUE(ctx.resolved);
            EXPECT_EQ(ctx.tenant.tenantId, tenantId);
            EXPECT_EQ(ctx.tenant.siteKey, "site-active");
        });
}

TEST(TenantResolutionGuardTest, ControlSuspendedSiteRejected) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ControlSuspendedSiteRejected", [&](Transaction& transaction) {
            DatabaseHelper db = testDb.GetDatabaseHelper();
            TableHelpers::Tenants tenants(db);
            TableHelpers::TenantRow row;
            row.siteKey = "site-suspended";
            row.databaseName = db.GetDatabaseName();
            row.displayName = "Suspended Site";
            row.status = std::string(DbSchema::kTenantStatusSuspended);
            row.maxConnections = 1;
            tenants.Insert(transaction, row);

            TransactionProviderPtr provider = MakeTestTransactionProvider(transaction);
            TenantResolutionGuard guard;
            guard.SetResolver(
                std::make_shared<Tenancy::ControlDbTenantResolver>(provider, db),
                Tenancy::TenancyMode::Control);

            crow::request req = MakeReq("/api/me");
            SetSite(req, "site-suspended");
            crow::response res;
            TenantResolutionGuard::context ctx;
            guard.before_handle(req, res, ctx);

            EXPECT_TRUE(res.is_completed());
            EXPECT_EQ(res.code, 421);
            EXPECT_FALSE(ctx.resolved);
            EXPECT_EQ(ErrorOf(res), "site_suspended");
        });
}

TEST(TenantResolutionGuardTest, ControlAllowsHealthHeaderless) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ControlAllowsHealthHeaderless", [&](Transaction& transaction) {
            TransactionProviderPtr provider = MakeTestTransactionProvider(transaction);
            DatabaseHelper db = testDb.GetDatabaseHelper();

            TenantResolutionGuard guard;
            guard.SetResolver(
                std::make_shared<Tenancy::ControlDbTenantResolver>(provider, db),
                Tenancy::TenancyMode::Control);

            crow::request req = MakeReq("/api/health");  // no header, control mode
            crow::response res;
            TenantResolutionGuard::context ctx;
            guard.before_handle(req, res, ctx);

            EXPECT_FALSE(res.is_completed());  // allow-listed even in control mode
            EXPECT_FALSE(ctx.resolved);
        });
}

}  // namespace
}  // namespace Endpoints
