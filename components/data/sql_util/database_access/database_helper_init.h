#pragma once

#include <string>
#include <string_view>

// Connection parameters for the production PostgreSQL connection. The
// constructor reads each field from a KNOTTYYOGA_DB_* environment variable and
// falls back to a caller-supplied / legacy dev default when the variable is
// unset or empty.
//
// The database name has no framework default — the application owns it and
// passes it as `defaultDatabaseName` (see business_logic/app_database_config.h).
// The KNOTTYYOGA_DB_NAME env var still overrides it when set, so operators can
// point a deploy at an alternate database without a rebuild.
//
// Recognized environment variables:
//   KNOTTYYOGA_DB_HOST        — host name or IP (default: legacy "postgresql"
//                                on Linux, "localhost" on Windows)
//   KNOTTYYOGA_DB_PORT        — TCP port (default: "5432")
//   KNOTTYYOGA_DB_USER        — login user (default: "docker")
//   KNOTTYYOGA_DB_PASSWORD    — login password (default: "docker")
//   KNOTTYYOGA_DB_NAME        — database name (default: defaultDatabaseName arg)
//   KNOTTYYOGA_DB_SSLMODE     — libpq sslmode: disable, allow, prefer,
//                                require, verify-ca, verify-full
//                                (default: "prefer")
//   KNOTTYYOGA_DB_SSLROOTCERT — path to a CA bundle, required for
//                                sslmode=verify-full against RDS
//                                (default: unset)
struct DatabaseHelperInit {
    explicit DatabaseHelperInit(std::string_view defaultDatabaseName);

    std::string user;
    std::string host;
    std::string password;
    std::string port;
    std::string dbname;
    std::string sslMode;
    std::string sslRootCert;

    // Build the libpq-style connection string, including sslmode and
    // sslrootcert when those fields are non-empty.
    std::string GetConnectionString() const;

    // Emit a single LogInfo line with host/port/db/user/sslmode/sslrootcert.
    // Deliberately omits the password. Called once at server startup so a
    // misconfigured environment is obvious in logs.
    void LogStartupInfo() const;
};
