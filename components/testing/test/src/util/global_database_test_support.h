#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "sql_util/database_common.h"
#include "sql_util/database_access/database_helper.h"
#include "sql_util/schema/database_info.h"

constexpr std::string_view kTestDatabaseName = "test_knottyyoga";

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
