#include "database_helper_init.h"

#include <cstdlib>
#include <string>
#include <string_view>

#include "util/env.h"
#include "util/logging.h"

namespace {

// Reads the canonical HONUWARE_DB_* variable, falling back to the legacy
// KNOTTYYOGA_DB_* name (the componentization Phase 1.1 rename convention, via
// Util::GetEnvWithFallback) and finally to `fallback` when neither resolves to a
// non-empty value. An empty resolved value is treated as "unset".
//
// The realistic inputs — canonical set to a real value, canonical absent with
// legacy set, or both absent — behave identically on every platform. The only
// edge that differs is a canonical var set to the *empty string*, which is not
// even constructible the same way (POSIX setenv keeps a present-but-empty var;
// Windows _putenv_s removes it), so this code deliberately does not rely on it.
std::string DbEnvOr(const char* canonical, const char* legacy,
                    std::string_view fallback) {
    const char* val = Util::GetEnvWithFallback(canonical, legacy);
    if (val == nullptr || val[0] == '\0') {
        return std::string(fallback);
    }
    return std::string(val);
}

}  // namespace

DatabaseHelperInit::DatabaseHelperInit(std::string_view defaultDatabaseName) {
#ifdef _WIN32
    constexpr std::string_view kDefaultHost = "localhost";
#else
    constexpr std::string_view kDefaultHost = "postgresql";
#endif

    // Debug/test builds default sslmode to empty so the connection string
    // matches the legacy format that the local PostgreSQL test container
    // expects. Release builds (NDEBUG defined) default to "prefer" so the
    // RDS-fronted production server gets TLS opportunistically. Either can
    // be overridden by the HONUWARE_DB_SSLMODE env var.
#ifdef NDEBUG
    constexpr std::string_view kDefaultSslMode = "prefer";
#else
    constexpr std::string_view kDefaultSslMode = "";
#endif

    user        = DbEnvOr("HONUWARE_DB_USER",        "KNOTTYYOGA_DB_USER",        "docker");
    host        = DbEnvOr("HONUWARE_DB_HOST",        "KNOTTYYOGA_DB_HOST",        kDefaultHost);
    password    = DbEnvOr("HONUWARE_DB_PASSWORD",    "KNOTTYYOGA_DB_PASSWORD",    "docker");
    port        = DbEnvOr("HONUWARE_DB_PORT",        "KNOTTYYOGA_DB_PORT",        "5432");
    dbname      = DbEnvOr("HONUWARE_DB_NAME",        "KNOTTYYOGA_DB_NAME",        defaultDatabaseName);
    sslMode     = DbEnvOr("HONUWARE_DB_SSLMODE",     "KNOTTYYOGA_DB_SSLMODE",     kDefaultSslMode);
    sslRootCert = DbEnvOr("HONUWARE_DB_SSLROOTCERT", "KNOTTYYOGA_DB_SSLROOTCERT", "");
}

std::string DatabaseHelperInit::GetConnectionString() const {
    std::string conn =
        "user=" + user +
        " host=" + host +
        " password=" + password +
        " port=" + port;
    // An empty database name must OMIT the key entirely rather than emit a bare
    // "dbname=". libpq's conninfo parser skips whitespace after "keyword=" and
    // takes the NEXT token as the value, so
    //     "... port=5432 dbname= sslmode=prefer"
    // parses dbname as the literal string "sslmode=prefer" and the connection
    // dies with: FATAL: database "sslmode=prefer" does not exist.
    //
    // The no-database bootstrap helper (MakeNoDatabaseHelper, which issues
    // CREATE DATABASE and so cannot name the database it is creating) depends on
    // the omission: with no dbname key, libpq defaults the database to the USER
    // NAME. Appending nothing here is what that code has always intended -- see
    // DatabaseHelperNoDatabase, whose comment says the string "omits dbname
    // entirely".
    //
    // This only bit when sslMode was non-empty, i.e. every RELEASE build
    // (sslMode defaults to "prefer" under NDEBUG). Debug builds default it to ""
    // and so happened to end the string at "dbname=", which libpq reads as an
    // empty value -- which is why Windows/Debug never saw it.
    if (!dbname.empty()) {
        conn += " dbname=" + dbname;
    }
    if (!sslMode.empty()) {
        conn += " sslmode=" + sslMode;
    }
    if (!sslRootCert.empty()) {
        conn += " sslrootcert=" + sslRootCert;
    }
    return conn;
}

void DatabaseHelperInit::LogStartupInfo() const {
    LogInfo()
        << "Database connection: host=" << host
        << " port=" << port
        << " dbname=" << dbname
        << " user=" << user
        << " sslmode=" << (sslMode.empty() ? "<unset>" : sslMode)
        << " sslrootcert=" << (sslRootCert.empty() ? "<unset>" : sslRootCert)
        << "\n";
}
