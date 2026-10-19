#include "sql_util/table_helpers/password_reset_tokens.h"

#include "db_schema/password_reset_tokens.h"
#include "sql_util/database_access/database_crud_helpers.h"

namespace TableHelpers {

namespace {

constexpr std::string_view kIncrementAttemptsSql =
    "UPDATE password_reset_tokens "
    "SET attempts = attempts + 1 "
    "WHERE person_id = $1 "
    "  AND attempts < $2::int "
    "  AND expires_at > now_us() "
    "RETURNING id, person_id, token_hash, expires_at, attempts, created_at";

}  // namespace

PasswordResetTokens::PasswordResetTokens(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

int64_t PasswordResetTokens::ReplaceForPerson(
    Transaction& transaction,
    int64_t personId,
    std::string_view tokenHash,
    int64_t expiresAtUs) {
    DeleteForPerson(transaction, personId);
    KeyValueTable row = {
        { std::string(DbSchema::kPasswordResetTokensPersonId), StringFromInt(personId) },
        { std::string(DbSchema::kPasswordResetTokensTokenHash), std::string(tokenHash) },
        { std::string(DbSchema::kPasswordResetTokensExpiresAt), StringFromInt(expiresAtUs) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        databaseHelper_,
        DbSchema::kPasswordResetTokensTable,
        row);
}

KeyValueTable PasswordResetTokens::GetForPerson(
    Transaction& transaction,
    int64_t personId) const {
    return DbCrud::GetRow(
        transaction,
        databaseHelper_,
        DbSchema::kPasswordResetTokensTable,
        DbSchema::kPasswordResetTokensPersonId,
        StringFromInt(personId));
}

KeyValueTable PasswordResetTokens::IncrementAttempts(
    Transaction& transaction,
    int64_t personId,
    int64_t attemptLimit) {
    KeyValueTableArray rows = transaction.RunSqlStatementReturningKeyValueTableArray(
        kIncrementAttemptsSql,
        StringFromInt(personId),
        StringFromInt(attemptLimit));
    if (rows.empty()) {
        return {};
    }
    return rows[0];
}

void PasswordResetTokens::DeleteForPerson(
    Transaction& transaction,
    int64_t personId) {
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kPasswordResetTokensTable,
        DbSchema::kPasswordResetTokensPersonId,
        StringFromInt(personId));
}

}  // namespace TableHelpers
