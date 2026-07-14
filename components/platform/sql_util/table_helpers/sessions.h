#pragma once

#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class Sessions
{
public:
    Sessions(DatabaseHelper databaseHelper);
    Sessions(const Sessions&) = default;
    Sessions& operator=(const Sessions&) = default;
    ~Sessions() = default;

    // Creates a session for personId, setting expires_at to now() + microsUntilExpires (in microseconds).
    // Returns the generated session id.
    int64_t AddSession(
        Transaction& transaction,
        int64_t personId,
        int64_t microsUntilExpires);

    // Lookups
    KeyValueTable LookupSessionById(Transaction& transaction, int64_t id) const;
    KeyValueTable LookupSessionByUuid(
        Transaction& transaction, std::string_view uuid) const;
    KeyValueTableArray LookupSessionByPerson(
        Transaction& transaction, int64_t personId) const;

    // Updates
    void UpdateLastSeen(Transaction& transaction, int64_t id);

    // Deletes
    void RemoveSessionsForUser(Transaction& transaction, int64_t personId);
    void RemoveSession(Transaction& transaction, int64_t sessionId);

private:
    DatabaseHelper databaseHelper_;
};

} // namespace TableHelpers