#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

// Phase 5.1 of the security review: helper for the login_attempts
// rate-limit bookkeeping table.
//
// Methods come in two shapes: writes (RecordAttempt) are normally
// invoked from a ThreadPool lambda after the synchronous response
// goes back; reads (RecentFailureCount*) run inline on the request
// path because the gate decision needs the latest count.
//
// Tests construct a helper directly; production code constructs one
// inside the ThreadPool lambda or the LoginGate.
class LoginAttempts {
public:
    LoginAttempts(DatabaseHelper databaseHelper);
    LoginAttempts(const LoginAttempts&) = default;
    LoginAttempts& operator=(const LoginAttempts&) = default;
    ~LoginAttempts() = default;

    // Inserts a single attempt row. `kind` is one of
    // kLoginAttemptsKindLogin / kLoginAttemptsKindVerify /
    // kLoginAttemptsKindRemember (see db_schema/login_attempts.h).
    // The attempted_at timestamp is taken from now_us() at insert
    // time so write-behind latency doesn't skew the gate's view of
    // recency.
    void RecordAttempt(
        Transaction& transaction,
        std::string_view emailLower,
        std::string_view ip,
        std::string_view kind,
        bool success);

    // Counts FAILED attempts for `(email, kind)` within the last
    // `windowMicros` microseconds. The gate compares this against
    // the per-email threshold secret.
    int64_t RecentFailureCountForEmail(
        Transaction& transaction,
        std::string_view emailLower,
        std::string_view kind,
        int64_t windowMicros);

    // Counts FAILED attempts for `(ip, kind)` within the last
    // `windowMicros` microseconds. Per-IP bucket — guards against an
    // attacker spreading attempts across many email accounts.
    int64_t RecentFailureCountForIp(
        Transaction& transaction,
        std::string_view ip,
        std::string_view kind,
        int64_t windowMicros);

    // Deletes rows older than `ageMicros` microseconds. Run by the
    // periodic cleanup job (Phase 5.8) so the table doesn't grow
    // unbounded.
    int64_t PurgeOlderThan(
        Transaction& transaction,
        int64_t ageMicros);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
