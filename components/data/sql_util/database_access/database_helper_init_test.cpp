#include "sql_util/database_access/database_helper_init.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

// The database name is now a constructor parameter (no framework default), so
// tests supply their own default and assert the env override wins over it.
// Decoupled from the app brand on purpose — this is framework code.
constexpr std::string_view kTestDefaultDbName = "test_default_db";

// All env vars that DatabaseHelperInit reads — the canonical HONUWARE_DB_* set
// AND the legacy KNOTTYYOGA_DB_* fallbacks. Tests scrub BOTH families on entry
// and exit so a leaked value from one test (or an ambient value from either
// family) cannot influence the next.
constexpr const char* kAllEnvVars[] = {
    "HONUWARE_DB_HOST",
    "HONUWARE_DB_PORT",
    "HONUWARE_DB_USER",
    "HONUWARE_DB_PASSWORD",
    "HONUWARE_DB_NAME",
    "HONUWARE_DB_SSLMODE",
    "HONUWARE_DB_SSLROOTCERT",
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

// Snapshot the current value of an env var (nullopt when unset). Copies the
// bytes into an owning string so the snapshot survives later setenv/putenv
// calls that may invalidate the getenv() pointer.
std::optional<std::string> GetEnv(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
}

// RAII guard: snapshots the relevant env vars, clears them so each test starts
// from a known-empty environment, and RESTORES the snapshot on destruction.
//
// Restoring (not clearing) on exit is load-bearing. The CI job exports
// HONUWARE_DB_* (and older configs the legacy KNOTTYYOGA_DB_*) into the ambient
// process environment, and OTHER suites in the same test binary open fresh
// database connections that read those vars (e.g. GlobalDatabaseTestSupport's
// EnsureNamedDatabase path via MakeNoDatabaseHelper). An unconditional
// ClearAll() on exit unset them for the rest of the process, so those later
// connections fell back to the default host ("postgresql") and failed name
// resolution wherever the DB host isn't literally "postgresql" — i.e. CI,
// where the service is "postgres". It was invisible locally because the dev
// container's host IS "postgresql". Scrubbing/restoring BOTH families keeps a
// leaked value in either name from influencing a hermetic test.
class EnvScope {
public:
    EnvScope() {
        for (const char* name : kAllEnvVars) {
            saved_.emplace_back(name, GetEnv(name));
        }
        ClearAll();
    }
    ~EnvScope() {
        for (const auto& entry : saved_) {
            if (entry.second) {
                SetEnv(entry.first, entry.second->c_str());
            } else {
                UnsetEnv(entry.first);
            }
        }
    }

private:
    std::vector<std::pair<const char*, std::optional<std::string>>> saved_;
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
    SetEnv("HONUWARE_DB_HOST", "");
    SetEnv("HONUWARE_DB_USER", "");

    DatabaseHelperInit init(kTestDefaultDbName);

#ifdef _WIN32
    EXPECT_EQ(init.host, "localhost");
#else
    EXPECT_EQ(init.host, "postgresql");
#endif
    EXPECT_EQ(init.user, "docker");
}

// --- Per-field env overrides (canonical HONUWARE_DB_* names) ---

TEST(DatabaseHelperInitTest, HostFromEnv) {
    EnvScope scope;
    SetEnv("HONUWARE_DB_HOST", "knottyyoga.xxxxxx.us-west-2.rds.amazonaws.com");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.host, "knottyyoga.xxxxxx.us-west-2.rds.amazonaws.com");
}

TEST(DatabaseHelperInitTest, PortFromEnv) {
    EnvScope scope;
    SetEnv("HONUWARE_DB_PORT", "6543");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.port, "6543");
}

TEST(DatabaseHelperInitTest, UserAndPasswordFromEnv) {
    EnvScope scope;
    SetEnv("HONUWARE_DB_USER", "app_user");
    SetEnv("HONUWARE_DB_PASSWORD", "s3cret");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.user, "app_user");
    EXPECT_EQ(init.password, "s3cret");
}

TEST(DatabaseHelperInitTest, DbNameFromEnv) {
    // The env var overrides the constructor default so operators can point a
    // deploy at an alternate database without a rebuild.
    EnvScope scope;
    SetEnv("HONUWARE_DB_NAME", "app_staging");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.dbname, "app_staging");
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
    SetEnv("HONUWARE_DB_SSLMODE", "verify-full");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.sslMode, "verify-full");
}

TEST(DatabaseHelperInitTest, SslRootCertFromEnv) {
    EnvScope scope;
    SetEnv("HONUWARE_DB_SSLROOTCERT", "/etc/honuware/rds-ca.pem");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.sslRootCert, "/etc/honuware/rds-ca.pem");
}

TEST(DatabaseHelperInitTest, AllFieldsFromEnv) {
    EnvScope scope;
    SetEnv("HONUWARE_DB_HOST", "host.example.com");
    SetEnv("HONUWARE_DB_PORT", "5433");
    SetEnv("HONUWARE_DB_USER", "appuser");
    SetEnv("HONUWARE_DB_PASSWORD", "pw");
    SetEnv("HONUWARE_DB_NAME", "appdb");
    SetEnv("HONUWARE_DB_SSLMODE", "require");
    SetEnv("HONUWARE_DB_SSLROOTCERT", "/tmp/ca.pem");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.host, "host.example.com");
    EXPECT_EQ(init.port, "5433");
    EXPECT_EQ(init.user, "appuser");
    EXPECT_EQ(init.password, "pw");
    EXPECT_EQ(init.dbname, "appdb");
    EXPECT_EQ(init.sslMode, "require");
    EXPECT_EQ(init.sslRootCert, "/tmp/ca.pem");
}

// --- Legacy KNOTTYYOGA_DB_* fallback + canonical precedence ---

TEST(DatabaseHelperInitTest, LegacyNameHonoredWhenCanonicalUnset) {
    // Old deploy environments that still set only KNOTTYYOGA_DB_* keep working
    // during the transition (Util::GetEnvWithFallback falls back to the legacy
    // name when the canonical HONUWARE_DB_* one is unset).
    EnvScope scope;
    SetEnv("KNOTTYYOGA_DB_HOST", "legacy-host.example.com");
    SetEnv("KNOTTYYOGA_DB_PORT", "5544");
    SetEnv("KNOTTYYOGA_DB_SSLMODE", "require");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.host, "legacy-host.example.com");
    EXPECT_EQ(init.port, "5544");
    EXPECT_EQ(init.sslMode, "require");
}

TEST(DatabaseHelperInitTest, CanonicalWinsWhenBothSet) {
    // When both names are set to real values, the canonical HONUWARE_DB_* value
    // wins ("read the new name first"). Covers a field and sslmode so the
    // precedence is proven through the same DbEnvOr path every field uses.
    // (The empty-string canonical edge is deliberately NOT tested: it isn't
    // constructible the same way across platforms — POSIX setenv keeps a
    // present-but-empty var, Windows _putenv_s removes it — so the code does not
    // rely on it. See DbEnvOr's comment.)
    EnvScope scope;
    SetEnv("HONUWARE_DB_HOST", "canonical-host");
    SetEnv("KNOTTYYOGA_DB_HOST", "legacy-host");
    SetEnv("HONUWARE_DB_SSLMODE", "disable");
    SetEnv("KNOTTYYOGA_DB_SSLMODE", "require");

    DatabaseHelperInit init(kTestDefaultDbName);

    EXPECT_EQ(init.host, "canonical-host");
    EXPECT_EQ(init.sslMode, "disable");
}

// --- GetConnectionString ---

TEST(DatabaseHelperInitTest, ConnectionStringIncludesAllRequiredKeys) {
    EnvScope scope;
    SetEnv("HONUWARE_DB_HOST", "host.example.com");
    SetEnv("HONUWARE_DB_PORT", "5433");
    SetEnv("HONUWARE_DB_USER", "appuser");
    SetEnv("HONUWARE_DB_PASSWORD", "pw");
    SetEnv("HONUWARE_DB_NAME", "appdb");

    DatabaseHelperInit init(kTestDefaultDbName);
    std::string conn = init.GetConnectionString();

    EXPECT_NE(conn.find("user=appuser"), std::string::npos);
    EXPECT_NE(conn.find("host=host.example.com"), std::string::npos);
    EXPECT_NE(conn.find("password=pw"), std::string::npos);
    EXPECT_NE(conn.find("port=5433"), std::string::npos);
    EXPECT_NE(conn.find("dbname=appdb"), std::string::npos);
}

TEST(DatabaseHelperInitTest, ConnectionStringOmitsDbNameWhenEmpty) {
    // MakeNoDatabaseHelper clears the name so libpq defaults the database to the
    // user name -- which only works if the key is omitted, not emitted empty.
    EnvScope scope;
    DatabaseHelperInit init(kTestDefaultDbName);
    init.dbname.clear();

    std::string conn = init.GetConnectionString();

    EXPECT_EQ(conn.find("dbname="), std::string::npos);
    // The rest of the string must survive the omission.
    EXPECT_NE(conn.find("user="), std::string::npos);
    EXPECT_NE(conn.find("port="), std::string::npos);
}

TEST(DatabaseHelperInitTest, EmptyDbNameDoesNotSwallowTheNextParameter) {
    // Regression. GetConnectionString used to append " dbname=" unconditionally.
    // libpq's conninfo parser skips whitespace after "keyword=" and takes the
    // NEXT token as the value, so
    //     "... port=5432 dbname= sslmode=prefer"
    // parsed dbname as the literal "sslmode=prefer", and every no-database
    // bootstrap connection died with:
    //     FATAL: database "sslmode=prefer" does not exist
    // That broke CREATE DATABASE in any build with a non-empty sslMode -- i.e.
    // every RELEASE build, since sslMode defaults to "prefer" under NDEBUG.
    // Debug builds default it to "" and ended the string at "dbname=", which
    // libpq reads as an empty value, which is why Windows/Debug never saw it.
    EnvScope scope;
    SetEnv("HONUWARE_DB_SSLMODE", "prefer");

    DatabaseHelperInit init(kTestDefaultDbName);
    init.dbname.clear();

    std::string conn = init.GetConnectionString();

    EXPECT_EQ(conn.find("dbname="), std::string::npos);
    // sslmode must still be its own parameter, not eaten as the database name.
    EXPECT_NE(conn.find("sslmode=prefer"), std::string::npos);
    // The exact shape libpq mis-parsed.
    EXPECT_EQ(conn.find("dbname= "), std::string::npos);
}

TEST(DatabaseHelperInitTest, ConnectionStringIncludesSslModeWhenSet) {
    EnvScope scope;
    SetEnv("HONUWARE_DB_SSLMODE", "require");

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
    SetEnv("HONUWARE_DB_SSLMODE", "verify-full");
    SetEnv("HONUWARE_DB_SSLROOTCERT", "/etc/honuware/rds-ca.pem");

    DatabaseHelperInit init(kTestDefaultDbName);
    std::string conn = init.GetConnectionString();

    EXPECT_NE(conn.find("sslmode=verify-full"), std::string::npos);
    EXPECT_NE(conn.find("sslrootcert=/etc/honuware/rds-ca.pem"),
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

// --- EnvScope hygiene ---

TEST(DatabaseHelperInitTest, EnvScopeRestoresAmbientValueOnExit) {
    // Regression: EnvScope used to ClearAll() on exit, which unset the ambient
    // DB env the CI job exports, sending later suites' fresh DB connections to
    // the default host ("postgresql") — a DNS failure in CI. The guard must
    // snapshot on entry and RESTORE on exit, not clear. Exercised on the
    // canonical HONUWARE_DB_* name, which is what CI now exports.
    const std::optional<std::string> original = GetEnv("HONUWARE_DB_HOST");

    SetEnv("HONUWARE_DB_HOST", "ambient-host");
    {
        EnvScope scope;
        // Inside the scope the var is cleared so the test starts hermetic.
        EXPECT_EQ(std::getenv("HONUWARE_DB_HOST"), nullptr);
        SetEnv("HONUWARE_DB_HOST", "inner-host");
    }
    // On exit the ambient value is restored, not cleared away.
    const char* host = std::getenv("HONUWARE_DB_HOST");
    ASSERT_NE(host, nullptr);
    EXPECT_STREQ(host, "ambient-host");

    // Restore the true ambient value so this test is itself hermetic.
    if (original) {
        SetEnv("HONUWARE_DB_HOST", original->c_str());
    } else {
        UnsetEnv("HONUWARE_DB_HOST");
    }
}

TEST(DatabaseHelperInitTest, EnvScopeRestoresLegacyUnsetVarOnExit) {
    // The complementary case, on a LEGACY name (proving the legacy family is
    // also snapshotted/restored): a var that was UNSET before the scope must be
    // unset again afterward (the snapshot restores "absent", not empty-string).
    const std::optional<std::string> original = GetEnv("KNOTTYYOGA_DB_PORT");
    UnsetEnv("KNOTTYYOGA_DB_PORT");
    {
        EnvScope scope;
        SetEnv("KNOTTYYOGA_DB_PORT", "9999");
    }
    EXPECT_EQ(std::getenv("KNOTTYYOGA_DB_PORT"), nullptr);

    // Restore the true ambient value so this test is itself hermetic.
    if (original) {
        SetEnv("KNOTTYYOGA_DB_PORT", original->c_str());
    }
}

}  // namespace
