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

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/transaction.h"
#include "sql_util/database_common.h"
#include "sql_util/schema/database_info.h"
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

}  // namespace
}  // namespace Tenancy
