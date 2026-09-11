#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "sql_util/database_common.h"
#include "sql_util/database_access/database_helper.h"
#include "sql_util/schema/database_info.h"

// honuware's OWN test database base name. Phase 10.2b: this used to be
// "test_knottyyoga" — the framework defaulting to one specific application's
// database, which knottyyoga then depended on by never naming its own. Every app
// now names its own base name; this default is honuware's and nothing else's.
constexpr std::string_view kTestDatabaseName = "honuware_test";

// Phase 10.2 — the platform token appended to every test database base name, so
// a Linux gate and a Windows run of the SAME repo drive different physical
// databases and can run concurrently against one shared PostgreSQL.
//
// Derived at COMPILE time rather than from an environment variable, deliberately:
// the harness DROPs and CREATEs whatever name it is handed, so an
// externally-supplied suffix would make an arbitrary string part of a destructive
// database operation — needing validation, a character allowlist, and a length
// check against PostgreSQL's 63-byte identifier cap. Compile-time detection
// cannot be misconfigured and needs none of that.
//
// The trade, accepted explicitly: with no override, two Linux gates from two
// checkouts of the SAME repo still collide. If that ever stops being acceptable
// the escape hatch is an override — which must then arrive WITH the validation
// described above, not without it.
#ifdef _WIN32
constexpr std::string_view kTestDatabasePlatformToken = "windows";
#else
constexpr std::string_view kTestDatabasePlatformToken = "linux";
#endif

// Compose the physical test database name as "<baseName>_<platformToken>".
//
// The token is a PARAMETER (defaulting to the compiled one) only so it can be
// tested for both platforms from a single build: the #ifdef above makes one
// branch unreachable per build, so a test asserting only the compiled default
// would prove nothing about the other platform. Production callers pass the
// base name alone.
std::string ComposeTestDatabaseName(
    std::string_view baseName,
    std::string_view platformToken = kTestDatabasePlatformToken);

// Reusable database test harness (honuware_testing). It is app-agnostic: the
// composed schema (framework + app tables) is passed IN by the caller rather
// than built here, so the harness creates exactly the tables it is handed and
// carries no dependency on any particular app's DatabaseInfo composition root.
class GlobalDatabaseTestSupport {
public:
    GlobalDatabaseTestSupport(const GlobalDatabaseTestSupport&) = delete;
    GlobalDatabaseTestSupport& operator=(const GlobalDatabaseTestSupport&) = delete;
    ~GlobalDatabaseTestSupport();

    // Initialize the primary test database (named by the injected DatabaseInfo,
    // e.g. "test_knottyyoga" or "honuware_test") with the given
    // composed schema. The caller (the app's test main) passes
    // MakeDatabaseInfo(); the harness never calls it directly. Uses the
    // create-once contract: all DDL is committed up front so per-test CreateTable
    // calls become no-ops via IF NOT EXISTS.
    static bool Initialize(const DbSchema::DatabaseInfo& databaseInfo);
    static void Shutdown();
    static GlobalDatabaseTestSupport& GetInstance();

    DatabaseHelper GetDatabaseHelper();
    pqxx::connection& GetConnection();

    // The composed schema the harness was initialized with. TestDatabaseUtil and
    // other helpers read it from here instead of calling MakeDatabaseInfo, so the
    // harness carries no dependency on the app's schema composition root.
    const DbSchema::DatabaseInfo& GetDatabaseInfo() const;

    // Pass a BASE name: the platform token is appended here, exactly as it is for
    // the primary database, so secondary databases are platform-qualified too
    // ("test_honuware_tenant_b_windows"). Do NOT pre-compose at the call site or
    // the suffix lands twice.
    //
    // Tenancy seam (`⇦ tenancy`): create + populate an ADDITIONAL named test
    // database once per run and return a DatabaseHelper bound to it. Idempotent
    // by name — repeat calls return the cached helper without recreating — using
    // the same create-once contract as the primary database. The multi-tenant
    // plan uses this for a second physical-isolation database
    // (e.g. test_honuware_tenant_b).
    DatabaseHelper EnsureNamedDatabase(
        std::string_view databaseName, const DbSchema::DatabaseInfo& databaseInfo);

private:
    GlobalDatabaseTestSupport();

    bool InitializeInternal(const DbSchema::DatabaseInfo& databaseInfo);
    DatabaseHelper CreateAndPopulateDatabase(
        std::string_view databaseName, const DbSchema::DatabaseInfo& databaseInfo);
    void SetupAllTables(
        DatabaseHelper& databaseHelper, const DbSchema::DatabaseInfo& databaseInfo);

    static GlobalDatabaseTestSupport* instance_;

    DatabaseHelper databaseHelper_;                         // primary DB
    DbSchema::DatabaseInfo databaseInfo_;                   // primary composed schema
    std::map<std::string, DatabaseHelper> namedDatabases_;  // additional named DBs
};
