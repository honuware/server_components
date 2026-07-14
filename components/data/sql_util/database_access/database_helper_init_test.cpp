#include "sql_util/database_access/database_helper_init.h"

#include <cstdlib>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

// The database name is now a constructor parameter (no framework default), so
// tests supply their own default and assert the env override wins over it.
// Decoupled from the app brand on purpose — this is framework code.
constexpr std::string_view kTestDefaultDbName = "test_default_db";

// All env vars that DatabaseHelperInit reads. Tests scrub these on entry and
// exit so a leaked value from one test cannot influence the next.
constexpr const char* kAllEnvVars[] = {
    "KNOTTYYOGA_DB_HOST",
    "KNOTTYYOGA_DB_PORT",
    "KNOTTYYOGA_DB_USER",
    "KNOTTYYOGA_DB_PASSWORD",
    "KNOTTYYOGA_DB_NAME",
    "KNOTTYYOGA_DB_SSLMODE",
    "KNOTTYYOGA_DB_SSLROOTCERT",
};

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, /*overwrite=*/1);
#endif
}

void UnsetEnv(const char* name) {
#ifdef _WIN32
    // On Windows, _putenv_s with an empty value removes the variable.
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

void ClearAll() {
    for (const char* name : kAllEnvVars) {
        UnsetEnv(name);
    }
}

// RAII guard: clears the relevant env vars on entry and again on destruction.
// Tests can call SetEnv inside the scope without worrying about leaking.
class EnvScope {
public:
    EnvScope() { ClearAll(); }
    ~EnvScope() { ClearAll(); }
};

// --- Defaults (no env vars set) ---

TEST(DatabaseHelperInitTest, DefaultsWhenEnvUnset) {
    EnvScope scope;

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.user, "docker");
    EXPECT_EQ(init.password, "docker");
    EXPECT_EQ(init.port, "5432");
    EXPECT_EQ(init.dbname, std::string(kTestDefaultDbName));
    EXPECT_EQ(init.sslRootCert, "");

#ifdef _WIN32
    EXPECT_EQ(init.host, "localhost");
#else
    EXPECT_EQ(init.host, "postgresql");
#endif

    // sslmode default depends on build mode: empty in debug/test (so the
    // local PostgreSQL container connection string matches its legacy form)
    // and "prefer" in release (so production RDS gets TLS).
#ifdef NDEBUG
    EXPECT_EQ(init.sslMode, "prefer");
#else
    EXPECT_EQ(init.sslMode, "");
#endif
}

TEST(DatabaseHelperInitTest, EmptyEnvFallsBackToDefault) {
    EnvScope scope;
    // An explicitly-empty value should be treated as "unset".
    SetEnv("KNOTTYYOGA_DB_HOST", "");
    SetEnv("KNOTTYYOGA_DB_USER", "");

    DatabaseHelperInit init(kTestDefaultDbName);

#ifdef _WIN32
    EXPECT_EQ(init.host, "localhost");
#else
    EXPECT_EQ(init.host, "postgresql");
#endif
    EXPECT_EQ(init.user, "docker");
}

// --- Per-field env overrides ---

TEST(DatabaseHelperInitTest, HostFromEnv) {
    EnvScope scope;
    SetEnv("KNOTTYYOGA_DB_HOST", "knottyyoga.xxxxxx.us-west-2.rds.amazonaws.com");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.host, "knottyyoga.xxxxxx.us-west-2.rds.amazonaws.com");
}

TEST(DatabaseHelperInitTest, PortFromEnv) {
    EnvScope scope;
    SetEnv("KNOTTYYOGA_DB_PORT", "6543");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.port, "6543");
}

TEST(DatabaseHelperInitTest, UserAndPasswordFromEnv) {
    EnvScope scope;
    SetEnv("KNOTTYYOGA_DB_USER", "knottyyoga_app");
    SetEnv("KNOTTYYOGA_DB_PASSWORD", "s3cret");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.user, "knottyyoga_app");
    EXPECT_EQ(init.password, "s3cret");
}

TEST(DatabaseHelperInitTest, DbNameFromEnv) {
    // The env var overrides the constructor default so operators can point a
    // deploy at an alternate database without a rebuild.
    EnvScope scope;
    SetEnv("KNOTTYYOGA_DB_NAME", "knottyyoga_staging");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.dbname, "knottyyoga_staging");
}

TEST(DatabaseHelperInitTest, DbNameUsesConstructorDefaultWhenEnvUnset) {
    // Proves the name really comes from the constructor argument (not a hidden
    // framework constant): a distinct default flows straight through.
    EnvScope scope;

    DatabaseHelperInit init("some_other_db");

    EXPECT_EQ(init.dbname, "some_other_db");
}

TEST(DatabaseHelperInitTest, DbNameEmptyDefaultWithoutEnvIsEmpty) {
    // The no-database code path passes "" as the default; with the env unset
    // that yields an empty dbname (the connection string then omits dbname).
    EnvScope scope;

    DatabaseHelperInit init("");

    EXPECT_EQ(init.dbname, "");
}

TEST(DatabaseHelperInitTest, SslModeFromEnv) {
    EnvScope scope;
    SetEnv("KNOTTYYOGA_DB_SSLMODE", "verify-full");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.sslMode, "verify-full");
}

TEST(DatabaseHelperInitTest, SslRootCertFromEnv) {
    EnvScope scope;
    SetEnv("KNOTTYYOGA_DB_SSLROOTCERT", "/etc/knottyyoga/rds-ca.pem");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.sslRootCert, "/etc/knottyyoga/rds-ca.pem");
}

TEST(DatabaseHelperInitTest, AllFieldsFromEnv) {
    EnvScope scope;
    SetEnv("KNOTTYYOGA_DB_HOST", "host.example.com");
    SetEnv("KNOTTYYOGA_DB_PORT", "5433");
    SetEnv("KNOTTYYOGA_DB_USER", "appuser");
    SetEnv("KNOTTYYOGA_DB_PASSWORD", "pw");
    SetEnv("KNOTTYYOGA_DB_NAME", "appdb");
    SetEnv("KNOTTYYOGA_DB_SSLMODE", "require");
    SetEnv("KNOTTYYOGA_DB_SSLROOTCERT", "/tmp/ca.pem");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.host, "host.example.com");
    EXPECT_EQ(init.port, "5433");
    EXPECT_EQ(init.user, "appuser");
    EXPECT_EQ(init.password, "pw");
    EXPECT_EQ(init.dbname, "appdb");
    EXPECT_EQ(init.sslMode, "require");
    EXPECT_EQ(init.sslRootCert, "/tmp/ca.pem");
}

// --- GetConnectionString ---

TEST(DatabaseHelperInitTest, ConnectionStringIncludesAllRequiredKeys) {
    EnvScope scope;
    SetEnv("KNOTTYYOGA_DB_HOST", "host.example.com");
    SetEnv("KNOTTYYOGA_DB_PORT", "5433");
    SetEnv("KNOTTYYOGA_DB_USER", "appuser");
    SetEnv("KNOTTYYOGA_DB_PASSWORD", "pw");
    SetEnv("KNOTTYYOGA_DB_NAME", "appdb");

    DatabaseHelperInit init(kTestDefaultDbName);
    std::string conn = init.GetConnectionString();

    EXPECT_NE(conn.find("user=appuser"), std::string::npos);
    EXPECT_NE(conn.find("host=host.example.com"), std::string::npos);
    EXPECT_NE(conn.find("password=pw"), std::string::npos);
    EXPECT_NE(conn.find("port=5433"), std::string::npos);
    EXPECT_NE(conn.find("dbname=appdb"), std::string::npos);
}

TEST(DatabaseHelperInitTest, ConnectionStringIncludesSslModeWhenSet) {
    EnvScope scope;
    SetEnv("KNOTTYYOGA_DB_SSLMODE", "require");

    DatabaseHelperInit init(kTestDefaultDbName);
    std::string conn = init.GetConnectionString();

    EXPECT_NE(conn.find("sslmode=require"), std::string::npos);
}

TEST(DatabaseHelperInitTest, ConnectionStringOmitsSslModeWhenEmpty) {
    EnvScope scope;
    DatabaseHelperInit init(kTestDefaultDbName);
    init.sslMode.clear();  // explicitly empty
    std::string conn = init.GetConnectionString();

    EXPECT_EQ(conn.find("sslmode="), std::string::npos);
}

TEST(DatabaseHelperInitTest, ConnectionStringIncludesSslRootCertWhenSet) {
    EnvScope scope;
    SetEnv("KNOTTYYOGA_DB_SSLMODE", "verify-full");
    SetEnv("KNOTTYYOGA_DB_SSLROOTCERT", "/etc/knottyyoga/rds-ca.pem");

    DatabaseHelperInit init(kTestDefaultDbName);
    std::string conn = init.GetConnectionString();

    EXPECT_NE(conn.find("sslmode=verify-full"), std::string::npos);
    EXPECT_NE(conn.find("sslrootcert=/etc/knottyyoga/rds-ca.pem"),
        std::string::npos);
}

TEST(DatabaseHelperInitTest, ConnectionStringOmitsSslRootCertWhenEmpty) {
    EnvScope scope;
    DatabaseHelperInit init(kTestDefaultDbName);
    EXPECT_TRUE(init.sslRootCert.empty());  // sanity: default is empty

    std::string conn = init.GetConnectionString();

    EXPECT_EQ(conn.find("sslrootcert="), std::string::npos);
}

TEST(DatabaseHelperInitTest, ConnectionStringDefaultSslModeMatchesBuildMode) {
    // Release builds default to sslmode=prefer (libpq tries TLS, falls back
    // if the server doesn't support it). Debug/test builds omit sslmode
    // entirely to match the legacy connection-string format the local
    // PostgreSQL test container expects.
    EnvScope scope;
    DatabaseHelperInit init(kTestDefaultDbName);
    std::string conn = init.GetConnectionString();

#ifdef NDEBUG
    EXPECT_NE(conn.find("sslmode=prefer"), std::string::npos);
#else
    EXPECT_EQ(conn.find("sslmode="), std::string::npos);
#endif
}

}  // namespace
