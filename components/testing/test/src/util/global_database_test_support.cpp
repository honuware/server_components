#include "global_database_test_support.h"

#include "database_helper_test.h"
#include "sql_util/database_access/database_helper_init.h"
#include "sql_util/database_access/database_helper_base.h"
#include "sql_util/database_access/database_util.h"
#include "sql_util/database_access/db_and_table_operations.h"
#include "sql_util/database_access/extensions.h"
#include "sql_util/database_access/transaction_impl.h"
#include "sql_util/schema/database_info.h"
#include "sql_util/stored_procedures/create_stored_procedures.h"

namespace {

// The physical test database name. Resolved once, at Initialize() time, from the
// injected DatabaseInfo's name — so each consuming repo drives its own database
// (knottyyoga -> "test_knottyyoga", honuware -> "honuware_test") and their suites
// can run concurrently against one Postgres instance without colliding on a shared
// database. Defaults to kTestDatabaseName for any helper built before Initialize.
std::string& ActiveTestDatabaseName() {
    static std::string name{kTestDatabaseName};
    return name;
}

class DatabaseHelperTest : public DatabaseHelperBase {
public:
    DatabaseHelperTest();
    DatabaseHelperTest(const DatabaseHelperInit& init);
    DatabaseHelperTest(const DatabaseHelperTest& ref) = delete;
    DatabaseHelperTest& operator=(const DatabaseHelperTest& ref) = delete;
    ~DatabaseHelperTest() = default;

    // Fetch the database connection object
    pqxx::connection& GetConnection() override;

    // Get the database name
    const std::string& GetDatabaseName() const override;

    // Run the provided code in the context of a transaction
    // in an exception block.
    void RunInTransaction(
        std::string_view description, DatabaseHelper::DatabaseFunc databaseFunc) override;

    bool IsTest() const override { return true; }

private:
    std::shared_ptr<pqxx::connection> connection_;
    std::string databaseName_;
};

DatabaseHelperTest::DatabaseHelperTest() : DatabaseHelperTest(DatabaseHelperInit{ActiveTestDatabaseName()}) {}

DatabaseHelperTest::DatabaseHelperTest(const DatabaseHelperInit& init)
    : databaseName_(init.dbname) {
    connection_ = std::make_shared<pqxx::connection>(
        init.GetConnectionString()
    );
}

pqxx::connection& DatabaseHelperTest::GetConnection() {
    return *connection_;
}

const std::string& DatabaseHelperTest::GetDatabaseName() const {
    return databaseName_;
}

struct HandleAbort {
    HandleAbort(pqxx::work& trans) : trans_(trans) {}
    ~HandleAbort() { trans_.abort(); }
    pqxx::work& trans_;
};

void DatabaseHelperTest::RunInTransaction(
    std::string_view description, DatabaseHelper::DatabaseFunc databaseFunc) {
    std::string transactionDescription =
        "Running transaction: " + std::string(description);
    pqxx::work trans(*connection_, transactionDescription);
    TransactionImpl transaction(trans);
    try {
        HandleAbort handleAbort(trans);
        databaseFunc(transaction);
    }
    catch(...) {
        throw;
    }
}

std::shared_ptr<DatabaseHelperBase> MakeDatabaseHelperTest(std::string_view databaseName = kTestDatabaseName) {
    // Force the ACTIVE test database name (resolved at Initialize() from the
    // injected DatabaseInfo) regardless of the passed name or any *_DB_NAME the
    // developer might have exported, so the test suite never wanders onto a real
    // database.
    const std::string& name = ActiveTestDatabaseName();
    DatabaseHelperInit init(name);
    init.dbname = name;
    auto databaseHelperTest = std::make_unique<DatabaseHelperTest>(init);
    return databaseHelperTest;
}

}  // namespace {

DatabaseHelper MakeTestDatabaseHelper(std::string_view databaseName) {
    return DatabaseHelper(MakeDatabaseHelperTest(databaseName));
}

GlobalDatabaseTestSupport* GlobalDatabaseTestSupport::instance_ = nullptr;

// Note that we start with a no-database helper to allow for database creation.
// databaseInfo_ starts empty and is replaced with the injected composed schema
// in InitializeInternal.
GlobalDatabaseTestSupport::GlobalDatabaseTestSupport()
    : databaseHelper_(MakeNoDatabaseHelper()),
      databaseInfo_(kTestDatabaseName) {
}

GlobalDatabaseTestSupport::~GlobalDatabaseTestSupport() {
}

bool GlobalDatabaseTestSupport::InitializeInternal(
    const DbSchema::DatabaseInfo& databaseInfo) {
    // Remember the composed schema so helpers (TestDatabaseUtil) can read it back
    // instead of re-deriving it from the app's MakeDatabaseInfo.
    databaseInfo_ = databaseInfo;

    // Resolve the physical test database name from the INJECTED schema, so each
    // consuming repo drives its own database (knottyyoga -> test_knottyyoga,
    // honuware -> honuware_test) and their suites can run concurrently without
    // colliding on one shared database.
    ActiveTestDatabaseName() = std::string(databaseInfo.GetDatabaseName());

    // The primary test database is just the first named database. databaseHelper_
    // starts as a no-database helper (see the constructor); rebind it to the
    // freshly created + populated primary database.
    databaseHelper_ = CreateAndPopulateDatabase(ActiveTestDatabaseName(), databaseInfo);

    // After the primary tables exist, make per-test CreateTable calls no-ops
    // via IF NOT EXISTS.
    DbOps::SetIfNotExistsMode(true);

    return true;
}

DatabaseHelper GlobalDatabaseTestSupport::CreateAndPopulateDatabase(
    std::string_view databaseName, const DbSchema::DatabaseInfo& databaseInfo) {
    // Drop + recreate the database using a no-database helper (CREATE DATABASE
    // cannot run against the database being created).
    DatabaseHelper noDatabaseHelper(MakeNoDatabaseHelper());
    noDatabaseHelper.RunInTransaction(
        "GlobalDatabaseTestSupport::CreateAndPopulateDatabase",
        [&](Transaction& transaction) {
            DbUtil::ClearDatabase(transaction, databaseName);
            DbUtil::MakeDatabase(transaction, databaseName);
        });

    // Connect a helper bound to the new database.
    DatabaseHelperInit init(databaseName);
    init.dbname = std::string(databaseName);
    DatabaseHelper databaseHelper(std::make_shared<DatabaseHelperTest>(init));

    // Apply session-level performance tuning for tests. Tests are serialized,
    // non-concurrent, and don't need durability.
    pqxx::nontransaction tuning(databaseHelper.GetConnection());
    tuning.exec("SET synchronous_commit = OFF");
    tuning.exec("SET work_mem = '256MB'");
    tuning.exec("SET maintenance_work_mem = '256MB'");
    tuning.commit();

    // Pre-create all tables in a single committed batch so that per-test
    // CreateTable calls become no-ops via IF NOT EXISTS.
    SetupAllTables(databaseHelper, databaseInfo);
    return databaseHelper;
}

DatabaseHelper GlobalDatabaseTestSupport::EnsureNamedDatabase(
    std::string_view databaseName, const DbSchema::DatabaseInfo& databaseInfo) {
    std::string key(databaseName);
    auto it = namedDatabases_.find(key);
    if (it != namedDatabases_.end()) {
        return it->second;
    }
    DatabaseHelper databaseHelper =
        CreateAndPopulateDatabase(databaseName, databaseInfo);
    namedDatabases_.emplace(key, databaseHelper);
    return databaseHelper;
}

void GlobalDatabaseTestSupport::SetupAllTables(
    DatabaseHelper& databaseHelper, const DbSchema::DatabaseInfo& databaseInfo) {
    // Build a single batch SQL string containing all DDL to minimize
    // round-trips to the database during startup.
    std::string batchSql;

    // Postgres extensions (citext, ...) must load before any table that
    // references one of their types. Phase 1.6 of the security review.
    batchSql += Extensions::GenerateCreateExtensionsSql();
    batchSql += "\n";

    // Stored procedures that must exist before table creation (e.g. now_us())
    batchSql += StoredProcedures::GenerateStoredProceduresBeforeTablesSql();
    batchSql += "\n";

    // All table creation DDL
    for (const std::string& tableName : databaseInfo.GetAllTables()) {
        batchSql += DbOps::GenerateCreateTableSql(databaseInfo, tableName);
        batchSql += ";\n";
    }

    // Stored procedures that depend on tables (e.g. get_admin_alerts_in_window())
    batchSql += StoredProcedures::GenerateStoredProceduresAfterTablesSql();

    // Execute all DDL in a single round-trip
    pqxx::work trans(databaseHelper.GetConnection(), "SetupAllTables");
    trans.exec(batchSql);
    trans.commit();
}

bool GlobalDatabaseTestSupport::Initialize(
    const DbSchema::DatabaseInfo& databaseInfo) {
    if (instance_) {
        return false;
    }
    instance_ = new GlobalDatabaseTestSupport();
    if( !instance_->InitializeInternal(databaseInfo)) {
        delete instance_;
        instance_ = nullptr;
        return false;
    }
    return true;
}

void GlobalDatabaseTestSupport::Shutdown() {
    delete instance_;
    instance_ = nullptr;
}

GlobalDatabaseTestSupport& GlobalDatabaseTestSupport::GetInstance() {
    return *instance_;
}

DatabaseHelper GlobalDatabaseTestSupport::GetDatabaseHelper() {
    return databaseHelper_;
}
pqxx::connection& GlobalDatabaseTestSupport::GetConnection() {
    return databaseHelper_.GetConnection();
}
const DbSchema::DatabaseInfo& GlobalDatabaseTestSupport::GetDatabaseInfo() const {
    return databaseInfo_;
}
