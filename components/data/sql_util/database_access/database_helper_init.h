#pragma once

#include <string>
#include <string_view>

// Connection parameters for the production PostgreSQL connection. The
// constructor reads each field from a HONUWARE_DB_* environment variable,
// falling back to the legacy KNOTTYYOGA_DB_* name (the componentization
// Phase 1.1 rename convention, via Util::GetEnvWithFallback) and then to a
// caller-supplied / dev default when neither is set or the value is empty.
//
// The database name has no framework default — the application owns it and
// passes it as `defaultDatabaseName` (see business_logic/app_database_config.h).
// The HONUWARE_DB_NAME env var still overrides it when set, so operators can
// point a deploy at an alternate database without a rebuild.
//
// Recognized environment variables (each with a legacy KNOTTYYOGA_DB_* fallback):
//   HONUWARE_DB_HOST        — host name or IP (default: "postgresql" on Linux,
//                              "localhost" on Windows)
//   HONUWARE_DB_PORT        — TCP port (default: "5432")
//   HONUWARE_DB_USER        — login user (default: "docker")
//   HONUWARE_DB_PASSWORD    — login password (default: "docker")
//   HONUWARE_DB_NAME        — database name (default: defaultDatabaseName arg)
//   HONUWARE_DB_SSLMODE     — libpq sslmode: disable, allow, prefer, require,
//                              verify-ca, verify-full
//                              (default: "prefer" in release, unset in debug)
//   HONUWARE_DB_SSLROOTCERT — path to a CA bundle, required for
//                              sslmode=verify-full against RDS (default: unset)

// TCP liveness options every connection string carries.
//
// A server holds its connections for days, mostly idle. On 10/15/2026 a
// `systemd-networkd` restart on the host flushed Docker's NAT state, RDS's
// keepalives stopped reaching the server, and RDS dropped the connection --
// while the server, which was not using it, kept the dead socket for 12 hours.
//
// - keepalives_*: probe after 60s idle, every 10s, give up after 3 misses, so a
//   dead socket is noticed in about 90s. The probes also go OUT through the
//   NAT, which re-creates its state before RDS gives up on the connection.
// - tcp_user_timeout: data sent but unacknowledged for 30s closes the socket.
//   Without it a statement sent into a black-holed connection waits out TCP's
//   ~15-minute retransmission schedule -- holding the provider's lock, so the
//   whole site would wait with it. A slow QUERY is unaffected: the server
//   acknowledges what was sent long before it answers.
//
// libpq ignores the options a platform lacks (keepalives_count and
// tcp_user_timeout on Windows) rather than refusing the connection.
inline constexpr std::string_view kConnectionLivenessOptions =
    "keepalives=1 keepalives_idle=60 keepalives_interval=10 "
    "keepalives_count=3 tcp_user_timeout=30000";

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
    // sslrootcert when those fields are non-empty, and always the TCP liveness
    // options (kConnectionLivenessOptions).
    std::string GetConnectionString() const;

    // Emit a single LogInfo line with host/port/db/user/sslmode/sslrootcert.
    // Deliberately omits the password. Called once at server startup so a
    // misconfigured environment is obvious in logs.
    void LogStartupInfo() const;
};
