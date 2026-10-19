#pragma once

#include <cstdint>
#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

// Finishing website Phase 11.2: CRUD for the forgot-password links
// (db_schema/password_reset_tokens.h), at most one per person.
//
// How a link is made, how a presented secret is compared and what a failure
// tells the caller all live in Auth::PasswordResetHelper; this class only
// reads and writes rows.
class PasswordResetTokens {
public:
    PasswordResetTokens(DatabaseHelper databaseHelper);
    PasswordResetTokens(const PasswordResetTokens&) = default;
    PasswordResetTokens& operator=(const PasswordResetTokens&) = default;
    ~PasswordResetTokens() = default;

    // Stores a new link for the person, deleting the one they already had
    // first, so asking twice leaves only the second link working. Returns the
    // new row's id.
    int64_t ReplaceForPerson(
        Transaction& transaction,
        int64_t personId,
        std::string_view tokenHash,
        int64_t expiresAtUs);

    // The person's pending link, or an empty table when they have none.
    KeyValueTable GetForPerson(
        Transaction& transaction,
        int64_t personId) const;

    // Counts one presented secret against the person's link. A single
    // conditional UPDATE, so two concurrent attempts can't both slip in under
    // the limit: it matches only a link that has not expired (expires_at >
    // now_us()) and has had fewer than `attemptLimit` attempts, and returns
    // that row as it is after the increment (token_hash included, for the
    // caller to compare). Empty when nothing matched — no link, an expired
    // one, or one that is out of tries.
    KeyValueTable IncrementAttempts(
        Transaction& transaction,
        int64_t personId,
        int64_t attemptLimit);

    void DeleteForPerson(Transaction& transaction, int64_t personId);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
