#include "token_cleanup_helper.h"

#include "sql_util/table_helpers/device_tokens.h"
#include "sql_util/table_helpers/email_verifications.h"

namespace Auth {

TokenCleanupHelper::TokenCleanupHelper(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

TokenCleanupResult TokenCleanupHelper::CleanupExpired(
    Transaction& transaction,
    int64_t asOfUs) {

    if (asOfUs == 0) {
        asOfUs = std::stoll(
            transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));
    }

    TableHelpers::DeviceTokens deviceTokens(databaseHelper_);
    TableHelpers::EmailVerifications emailVerifications(
        databaseHelper_, /*secretsHelper=*/nullptr);

    TokenCleanupResult result;
    result.deviceTokensDeleted = deviceTokens.DeleteExpired(transaction, asOfUs);
    result.emailVerificationsDeleted =
        emailVerifications.DeleteExpired(transaction, asOfUs);
    return result;
}

}  // namespace Auth
