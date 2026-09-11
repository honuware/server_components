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

// --- Phase 10.2: platform-qualified test database names ---------------------
//
// The platform token is chosen by #ifdef, so one branch is unreachable in any
// given build. Asserting only the compiled default would prove nothing about the
// other platform — which is exactly the half that breaks a Linux gate. So these
// pass the token explicitly and cover both.

TEST(GlobalDatabaseTestSupportTest, ComposeTestDatabaseNameAppendsPlatformToken) {
    EXPECT_EQ(ComposeTestDatabaseName("honuware_test", "windows"),
              "honuware_test_windows");
    EXPECT_EQ(ComposeTestDatabaseName("honuware_test", "linux"),
              "honuware_test_linux");
    // The three real base names, both ways — these are the six databases the
    // arrangement is meant to produce.
    EXPECT_EQ(ComposeTestDatabaseName("test_knottyyoga", "linux"),
              "test_knottyyoga_linux");
    EXPECT_EQ(ComposeTestDatabaseName("test_communityfinder", "windows"),
              "test_communityfinder_windows");
}

TEST(GlobalDatabaseTestSupportTest, ComposeTestDatabaseNamePreservesBaseName) {
    // The base name must survive untouched — a suffix that mangled it would send
    // the suite at a different database, and the suite would still pass.
    const std::string composed =
        ComposeTestDatabaseName("test_communityfinder", "linux");
    EXPECT_EQ(composed.rfind("test_communityfinder", 0), 0u)
        << "composed name must START with the app-supplied base name";
    EXPECT_EQ(composed, std::string("test_communityfinder") + "_linux");
}

TEST(GlobalDatabaseTestSupportTest, ComposeTestDatabaseNameEmptyTokenLeavesBaseUnchanged) {
    // No trailing underscore when there is no token. Not a supported
    // configuration today, but it is the shape an override would take, and a
    // stray "name_" would be a silently different database.
    EXPECT_EQ(ComposeTestDatabaseName("honuware_test", ""), "honuware_test");
}

TEST(GlobalDatabaseTestSupportTest, CompiledPlatformTokenMatchesBuildPlatform) {
#ifdef _WIN32
    EXPECT_EQ(kTestDatabasePlatformToken, "windows");
#else
    EXPECT_EQ(kTestDatabasePlatformToken, "linux");
#endif
    // Whatever the platform, the default argument must agree with the constant.
    EXPECT_EQ(ComposeTestDatabaseName("base"),
              ComposeTestDatabaseName("base", kTestDatabasePlatformToken));
}

TEST(GlobalDatabaseTestSupportTest, ActiveDatabaseIsPlatformQualified) {
    // The end-to-end check: the database this suite is ACTUALLY connected to
    // carries the platform token. PostgreSQL answers from the live connection, so
    // this cannot pass by agreeing with a constant that was never applied.
    //
    // Deliberately does NOT assume the base name. This file compiles into every
    // consuming app's suite as well as honuware's own, and each app supplies its
    // own base name — asserting honuware's would fail in knottyyoga and
    // communityfinder for the wrong reason. The invariant that actually belongs
    // to the framework is "whatever the base name, the physical database is
    // platform-qualified and matches the injected schema".
    const std::string expectedSuffix =
        "_" + std::string(kTestDatabasePlatformToken);

    DatabaseHelper helper =
        GlobalDatabaseTestSupport::GetInstance().GetDatabaseHelper();
    std::string actual;
    helper.RunInTransaction("active-db-name", [&](Transaction& transaction) {
        actual =
            transaction.RunSqlStatementReturningOneValue("SELECT current_database()");
    });

    // Record the physical name so a run REPORTS which database it touched rather
    // than only asserting a property of it. Shows up in --gtest_output=xml, which
    // is what makes "did the Linux gate really use a different database?"
    // answerable after the fact instead of by inference.
    RecordProperty("active_database", actual);

    ASSERT_GT(actual.size(), expectedSuffix.size())
        << "database name '" << actual << "' is not longer than the suffix alone";
    EXPECT_EQ(actual.substr(actual.size() - expectedSuffix.size()), expectedSuffix)
        << "the running suite is on '" << actual
        << "', which is not platform-qualified";

    // ...and the harness's own view agrees with the physical database. They are
    // composed at the app boundary precisely so they cannot drift apart, since
    // DatabaseInfo has no name mutator; this asserts they have not.
    EXPECT_EQ(GlobalDatabaseTestSupport::GetInstance().GetDatabaseInfo().GetDatabaseName(),
              actual);
}

}  // namespace
