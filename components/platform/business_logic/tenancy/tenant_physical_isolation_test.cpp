// Tenancy plan Phase 3.4 — physical-isolation smoke suite. Proves what the
// two-control-rows-over-one-database routing test structurally cannot: that two
// tenant providers built over two DIFFERENT databases reach two genuinely
// separate data planes. Uses GlobalDatabaseTestSupport::EnsureNamedDatabase to
// stand up a second real database (test_honuware_tenant_b) carrying the same
// composed schema, then exercises isolation through plain SQL.
//
// Every transaction here uses DatabaseHelper::RunInTransaction, which ABORTS at
// the end, so neither the primary test database nor the second database is
// mutated — the suite is repeatable with no cross-run pollution.

#include <string>

#include <gtest/gtest.h>

#include "business_logic/auth/auth_cookies.h"
#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/transaction.h"
#include "sql_util/database_common.h"
#include "sql_util/schema/database_info.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_at_rest.h"
#include "util/secrets/secrets_helper.h"
#include "test/src/util/global_database_test_support.h"

namespace Tenancy {
namespace {

constexpr std::string_view kTenantBDatabase = "test_honuware_tenant_b";

TEST(TenantPhysicalIsolationTest, ProvidersReachSeparatePhysicalDatabases) {
    auto& support = GlobalDatabaseTestSupport::GetInstance();
    const DbSchema::DatabaseInfo& info = support.GetDatabaseInfo();
    DatabaseHelper dbA = support.GetDatabaseHelper();
    DatabaseHelper dbB = support.EnsureNamedDatabase(kTenantBDatabase, info);

    // They are genuinely different physical databases.
    std::string nameA;
    std::string nameB;
    dbA.RunInTransaction("iso-name-a", [&](Transaction& tx) {
        nameA = tx.RunSqlStatementReturningOneValue("SELECT current_database()");
    });
    dbB.RunInTransaction("iso-name-b", [&](Transaction& tx) {
        nameB = tx.RunSqlStatementReturningOneValue("SELECT current_database()");
    });
    EXPECT_NE(nameA, nameB);
    EXPECT_EQ(nameB, std::string(kTenantBDatabase));

    // A row inserted through dbA's transaction is invisible on dbB (a separate
    // physical database), while dbA sees its own row. Nested so dbA's insert is
    // live while dbB is queried; both abort, so nothing persists.
    dbA.RunInTransaction("iso-people-a", [&](Transaction& txA) {
        txA.RunSqlStatement(
            "INSERT INTO people (email, first_name, last_name, password_hash) "
            "VALUES ('iso-marker@a.test', 'Iso', 'Marker', 'hash')");
        std::string onA = txA.RunSqlStatementReturningOneValue(
            "SELECT COUNT(*) FROM people WHERE email = 'iso-marker@a.test'");
        EXPECT_EQ(onA, "1");
        dbB.RunInTransaction("iso-people-b", [&](Transaction& txB) {
            std::string onB = txB.RunSqlStatementReturningOneValue(
                "SELECT COUNT(*) FROM people WHERE email = 'iso-marker@a.test'");
            EXPECT_EQ(onB, "0");
        });
    });

    // config_secrets is per-database too: a value written on dbA is not visible on
    // dbB — this is what makes per-tenant secrets (Phase 4.1) independent.
    dbA.RunInTransaction("iso-secret-a", [&](Transaction& txA) {
        txA.RunSqlStatement(
            "INSERT INTO config_secrets (name, value) "
            "VALUES ('iso_test_secret', 'a-only')");
        dbB.RunInTransaction("iso-secret-b", [&](Transaction& txB) {
            std::string onB = txB.RunSqlStatementReturningOneValue(
                "SELECT COUNT(*) FROM config_secrets "
                "WHERE name = 'iso_test_secret'");
            EXPECT_EQ(onB, "0");
        });
    });
}

TEST(TenantPhysicalIsolationTest, SecondDatabaseHasIndependentSchemaAndSequence) {
    auto& support = GlobalDatabaseTestSupport::GetInstance();
    const DbSchema::DatabaseInfo& info = support.GetDatabaseInfo();
    DatabaseHelper dbB = support.EnsureNamedDatabase(kTenantBDatabase, info);

    // The second database carries its own framework schema with its own BIGSERIAL
    // sequence: an insert returns a valid generated id from ITS people_id_seq,
    // independent of the primary database's. Aborted, so nothing persists.
    dbB.RunInTransaction("seq-b", [&](Transaction& tx) {
        std::string id = tx.RunSqlStatementReturningOneValue(
            "INSERT INTO people (email, first_name, last_name, password_hash) "
            "VALUES ('seq@b.test', 'Seq', 'B', 'hash') RETURNING id");
        EXPECT_FALSE(id.empty());
        EXPECT_GT(std::stoll(id), 0);
    });
}

// Phase 4.1: the per-tenant SecretsHelper honors the physical boundary — a secret
// written through tenant A's helper is invisible through tenant B's, because they
// read different physical config_secrets tables. This is what makes per-tenant
// secrets independent (§1.6). Both SecretsHelpers share the one GLOBAL at-rest key.
TEST(TenantPhysicalIsolationTest, PerTenantSecretsHelpersAreIsolated) {
    auto& support = GlobalDatabaseTestSupport::GetInstance();
    const DbSchema::DatabaseInfo& info = support.GetDatabaseInfo();
    DatabaseHelper dbA = support.GetDatabaseHelper();
    DatabaseHelper dbB = support.EnsureNamedDatabase(kTenantBDatabase, info);

    Secrets::SecretsAtRestPtr secretsAtRest =
        Secrets::MakeSecretsAtRest(/*isProd=*/false);
    Secrets::SecretsHelperPtr secretsA =
        Secrets::MakeSecretsHelper(dbA, secretsAtRest);
    Secrets::SecretsHelperPtr secretsB =
        Secrets::MakeSecretsHelper(dbB, secretsAtRest);

    dbA.RunInTransaction("secrets-iso-a", [&](Transaction& txA) {
        secretsA->AddSecret(txA, "iso_secret_key", "a-only");
        EXPECT_EQ(secretsA->LookupSecret(txA, "iso_secret_key"), "a-only");
        dbB.RunInTransaction("secrets-iso-b", [&](Transaction& txB) {
            // Different physical config_secrets → the key is absent on B.
            EXPECT_EQ(secretsB->LookupSecret(txB, "iso_secret_key"), "");
        });
    });
}

// Phase 4.4: prod auth cookies derive their Domain from the resolving tenant's
// own website_address secret (BuildAuthCookieProperties reads it through the
// per-tenant SecretsHelper — Phase 4.1), so two tenants get two cookie domains.
// This is what keeps a browser from mixing tenants' session cookies.
TEST(TenantPhysicalIsolationTest, PerTenantCookieDomainDerivesFromTenantWebsite) {
    auto& support = GlobalDatabaseTestSupport::GetInstance();
    const DbSchema::DatabaseInfo& info = support.GetDatabaseInfo();
    DatabaseHelper dbA = support.GetDatabaseHelper();
    DatabaseHelper dbB = support.EnsureNamedDatabase(kTenantBDatabase, info);

    Secrets::SecretsAtRestPtr secretsAtRest =
        Secrets::MakeSecretsAtRest(/*isProd=*/false);
    Secrets::SecretsHelperPtr secretsA =
        Secrets::MakeSecretsHelper(dbA, secretsAtRest);
    Secrets::SecretsHelperPtr secretsB =
        Secrets::MakeSecretsHelper(dbB, secretsAtRest);

    dbA.RunInTransaction("cookie-domain-a", [&](Transaction& txA) {
        secretsA->AddSecret(txA, Secrets::kWebsiteAddress, "tenant-a.example");
        Auth::CookieProperties propsA = Auth::BuildAuthCookieProperties(
            txA, secretsA, /*isProd=*/true, /*httpOnly=*/true, /*maxAge=*/100);
        EXPECT_EQ(propsA.domain, "tenant-a.example");
        EXPECT_TRUE(propsA.secure);

        dbB.RunInTransaction("cookie-domain-b", [&](Transaction& txB) {
            secretsB->AddSecret(txB, Secrets::kWebsiteAddress, "tenant-b.example");
            Auth::CookieProperties propsB = Auth::BuildAuthCookieProperties(
                txB, secretsB, /*isProd=*/true, /*httpOnly=*/true, /*maxAge=*/100);
            EXPECT_EQ(propsB.domain, "tenant-b.example");
        });
    });
}

}  // namespace
}  // namespace Tenancy
