#include "global_database_test_support.h"

#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "sql_util/database_common.h"
#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/transaction.h"
#include "sql_util/schema/database_info.h"

namespace {

constexpr std::string_view kNamedDb = "test_honuware_named_db";

// The named-database seam is meant for the tenant plan's physical-isolation
// suite, which creates a SECOND real database carrying the same composed schema
// (framework + app). We reuse the harness's injected composed schema rather than
// a toy schema on purpose: SetupAllTables always installs the framework stored
// procedures (e.g. get_admin_alerts_in_window(), which returns SETOF
// admin_alerts), so any named database must carry the framework tables those
// procedures reference. A one-table toy schema cannot satisfy that.
TEST(GlobalDatabaseTestSupportTest, EnsureNamedDatabaseCreatesComposedSchema) {
    const DbSchema::DatabaseInfo& info =
        GlobalDatabaseTestSupport::GetInstance().GetDatabaseInfo();
    DatabaseHelper helper =
        GlobalDatabaseTestSupport::GetInstance().EnsureNamedDatabase(kNamedDb, info);

    // The composed schema was created in the named database: insert + read back a
    // person inside an aborted transaction so nothing persists. This succeeds
    // only if EnsureNamedDatabase created the `people` table in kNamedDb.
    helper.RunInTransaction("named-db-check", [&](Transaction& transaction) {
        transaction.RunSqlStatement(
            "INSERT INTO people (email, first_name, last_name, password_hash) "
            "VALUES ('named@test.com', 'Named', 'User', 'hash')");
        std::string email = transaction.RunSqlStatementReturningOneValue(
            "SELECT email FROM people WHERE first_name = 'Named'");
        EXPECT_EQ(email, "named@test.com");
    });
}

TEST(GlobalDatabaseTestSupportTest, EnsureNamedDatabaseIsIdempotent) {
    const DbSchema::DatabaseInfo& info =
        GlobalDatabaseTestSupport::GetInstance().GetDatabaseInfo();
    auto& support = GlobalDatabaseTestSupport::GetInstance();
    DatabaseHelper first = support.EnsureNamedDatabase(kNamedDb, info);
    // Second call returns the cached helper for the SAME database without
    // recreating it (create-once). The cached DatabaseHelper shares the exact
    // same underlying connection object, which a fresh create would not — this
    // proves the named database was not dropped and rebuilt.
    DatabaseHelper second = support.EnsureNamedDatabase(kNamedDb, info);
    EXPECT_EQ(&first.GetConnection(), &second.GetConnection());
}

TEST(GlobalDatabaseTestSupportTest, PrimaryDatabaseHasInjectedFrameworkTables) {
    // main() initialized the primary database from the injected composed schema, so
    // a framework table (people) exists. A COUNT(*) succeeds only if the table was
    // created. This is a framework-harness test, so it asserts only framework
    // tables; whether an app injected its own tables on top is an app-side concern.
    DatabaseHelper helper =
        GlobalDatabaseTestSupport::GetInstance().GetDatabaseHelper();
    helper.RunInTransaction("primary-check", [&](Transaction& transaction) {
        std::string people = transaction.RunSqlStatementReturningOneValue(
            "SELECT COUNT(*) FROM people");
        EXPECT_FALSE(people.empty());
    });
}

TEST(GlobalDatabaseTestSupportTest, GetDatabaseInfoReturnsInjectedComposedSchema) {
    // The harness hands back exactly the schema it was initialized with — this is
    // what TestDatabaseUtil now reads instead of calling MakeDatabaseInfo, which
    // is what keeps the harness free of the app schema composition root.
    const DbSchema::DatabaseInfo& info =
        GlobalDatabaseTestSupport::GetInstance().GetDatabaseInfo();
    StringArray tables = info.GetAllTables();
    bool hasPeople = false;
    for (const std::string& table : tables) {
        if (table == "people") hasPeople = true;
    }
    EXPECT_TRUE(hasPeople);   // framework table, present in every honuware schema
}

}  // namespace
