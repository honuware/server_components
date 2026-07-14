#pragma once

#include <cstdint>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/transaction.h"

namespace Auth {

struct TokenCleanupResult {
    int64_t deviceTokensDeleted = 0;
    int64_t emailVerificationsDeleted = 0;
};

class TokenCleanupHelper {
public:
    explicit TokenCleanupHelper(DatabaseHelper databaseHelper);

    // Deletes expired device tokens and email verifications.
    // If asOfUs is 0, looks up now_us() in the database and uses that.
    TokenCleanupResult CleanupExpired(Transaction& transaction, int64_t asOfUs = 0);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace Auth
