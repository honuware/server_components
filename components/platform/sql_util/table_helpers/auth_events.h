#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

// Phase 9.1 of the security review: forensic auth-event log helper.
//
// All writes go through RecordEvent. Reads are intended for
// incident-investigation workflows or admin reports — there's no
// generic CRUD path to this table.
class AuthEvents {
public:
    AuthEvents(DatabaseHelper databaseHelper);
    AuthEvents(const AuthEvents&) = default;
    AuthEvents& operator=(const AuthEvents&) = default;
    ~AuthEvents() = default;

    // Inserts a single event row. `personId` of 0 (or any
    // non-positive) is treated as "unknown person" and stored as
    // NULL — e.g., a login_failure on an unrecognised email.
    // `detailJson` is free-form; pass "" when there's no useful
    // context to capture. when_us is `now_us()` at insert time so
    // a write-behind / batched recorder doesn't backdate rows.
    void RecordEvent(
        Transaction& transaction,
        std::string_view kind,
        int64_t personId,
        std::string_view ip,
        std::string_view userAgent,
        std::string_view detailJson);

    // Counts events of `kind` in the last `windowMicros`. Used by
    // tests and (eventually) by alerting / digest endpoints.
    int64_t CountRecentByKind(
        Transaction& transaction,
        std::string_view kind,
        int64_t windowMicros);

    // Returns all rows of `kind` for `personId` in the last
    // `windowMicros`. Used by tests to read back the rows the
    // emission tests just wrote. Most-recent first.
    KeyValueTableArray RecentByPersonAndKind(
        Transaction& transaction,
        int64_t personId,
        std::string_view kind,
        int64_t windowMicros);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
