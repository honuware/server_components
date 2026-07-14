#include "device_tokens.h"

#include <stdexcept>

#include "db_schema/device_tokens.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

DeviceTokens::DeviceTokens(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

int64_t DeviceTokens::AddDeviceToken(
    Transaction& transaction,
    int64_t personId,
    std::string_view secretHash,
    int64_t microsUntilExpires) {
    std::string expiresExpr =
        "(extract(epoch FROM now()) * 1000000)::bigint + " + StringFromInt(microsUntilExpires);

    KeyValueTable kv = {
        { std::string(DbSchema::kDeviceTokensPersonId), StringFromInt(personId) },
        { std::string(DbSchema::kDeviceTokensSecretHash), std::string(secretHash) },
        { std::string(DbSchema::kDeviceTokensExpiresAt), expiresExpr }
    };
    StringSet allowed = { expiresExpr };

    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        databaseHelper_,
        DbSchema::kDeviceTokensTable,
        kv,
        allowed);
}

KeyValueTable DeviceTokens::LookupDeviceTokenById(Transaction& transaction, int64_t id) const {
    KeyValueTable row = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kDeviceTokensTable,
        DbSchema::kDeviceTokensId,
        StringFromInt(id));
    if (row.empty()) {
        throw std::runtime_error("DeviceTokens::LookupDeviceTokenById - not found");
    }
    return row;
}

KeyValueTable DeviceTokens::LookupDeviceTokenByUuid(Transaction& transaction, std::string_view uuid) const {
    KeyValueTable row = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kDeviceTokensTable,
        DbSchema::kDeviceTokensUuid,
        uuid);
    if (row.empty()) {
        throw std::runtime_error("DeviceTokens::LookupDeviceTokenByUuid - not found");
    }
    return row;
}

KeyValueTableArray DeviceTokens::LookupDeviceTokensByPerson(Transaction& transaction, int64_t personId) const {
    KeyValueTable filter = {
        { std::string(DbSchema::kDeviceTokensPersonId), StringFromInt(personId) }
    };
    KeyValueTableArray keyValueTableArray = DbCrud::GetRowsByValuesWithOrderBy(
        transaction,
        databaseHelper_,
        DbSchema::kDeviceTokensTable,
        filter,
        DbSchema::kDeviceTokensId,
        true);
    if (keyValueTableArray.empty()) {
        throw std::runtime_error("DeviceTokens::LookupDeviceTokensByPerson - not found");
    }
    return keyValueTableArray;
}

KeyValueTable DeviceTokens::LookupDeviceTokenBySecretHash(Transaction& transaction, std::string_view secretHash) const {
    KeyValueTable row = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kDeviceTokensTable,
        DbSchema::kDeviceTokensSecretHash,
        secretHash);
    if (row.empty()) {
        throw std::runtime_error("DeviceTokens::LookupDeviceTokenBySecretHash - not found");
    }
    return row;
}

bool DeviceTokens::IsValid(Transaction& transaction, int64_t id) const {
    KeyValueTable row = LookupDeviceTokenById(transaction, id);

    const std::string& revoked = row.at(std::string(DbSchema::kDeviceTokensRevoked));
    if (revoked == "t" || revoked == "true" || revoked == "1") {
        return false;
    }

    int64_t nowUs = std::stoll(transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));
    int64_t expires = std::stoll(row.at(std::string(DbSchema::kDeviceTokensExpiresAt)));
    return nowUs <= expires;
}

void DeviceTokens::Revoke(Transaction& transaction, int64_t id) {
    (void)LookupDeviceTokenById(transaction, id); // throws if missing
    std::string trueLiteral = "t";
    KeyValueTable kv = {
        { std::string(DbSchema::kDeviceTokensRevoked), trueLiteral }
    };
    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kDeviceTokensTable,
        DbSchema::kDeviceTokensId,
        StringFromInt(id),
        kv);
}

bool DeviceTokens::UpdateDeviceToken(
    Transaction& transaction,
    int64_t id,
    std::string_view secretHash,
    int64_t microsUntilExpires) {
    // Must exist
    KeyValueTable existing = LookupDeviceTokenById(transaction, id);

    // Check validity (not expired and not revoked)
    if (!IsValid(transaction, id)) {
        return false;
    }

    std::string expiresExpr =
        "(extract(epoch FROM now()) * 1000000)::bigint + " + StringFromInt(microsUntilExpires);
    std::string rowNowUs = "now_us()";
    std::string getRandomUuid = "gen_random_uuid()";

    KeyValueTable kv = {
        { std::string(DbSchema::kDeviceTokensSecretHash), std::string(secretHash) },
        { std::string(DbSchema::kDeviceTokensExpiresAt), expiresExpr },
        { std::string(DbSchema::kDeviceTokensLastUsedAt), rowNowUs },
        { std::string(DbSchema::kDeviceTokensUuid), getRandomUuid }
    };
    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kDeviceTokensTable,
        DbSchema::kDeviceTokensId,
        StringFromInt(id),
        kv,
        { expiresExpr, rowNowUs, getRandomUuid });

    return true;
}

bool DeviceTokens::ConsumeAndRotate(
    Transaction& transaction,
    std::string_view oldSecretHash,
    std::string_view oldUuid,
    std::string_view newSecretHash,
    int64_t microsUntilExpires,
    int64_t& outPersonId,
    std::string& outNewUuid) {
    // The expires_at expression has to be inlined because libpqxx
    // exec_params can only bind values, not column expressions.
    // microsUntilExpires is an int64 produced server-side, not user
    // input, so concatenating it is safe.
    std::string sql =
        "UPDATE device_tokens SET "
        "  secret_hash = $3, "
        "  uuid = gen_random_uuid(), "
        "  last_used_at = now_us(), "
        "  expires_at = now_us() + " + StringFromInt(microsUntilExpires) + " "
        "WHERE secret_hash = $1 "
        "  AND uuid::text = $2 "
        "  AND NOT revoked "
        "  AND now_us() < expires_at "
        "RETURNING person_id, uuid";

    KeyValueTableArray rows = transaction.RunSqlStatementReturningKeyValueTableArray(
        sql,
        std::string(oldSecretHash),
        std::string(oldUuid),
        std::string(newSecretHash));

    if (rows.empty()) {
        outPersonId = 0;
        outNewUuid.clear();
        return false;
    }

    const KeyValueTable& row = rows[0];
    outPersonId = std::stoll(row.at(std::string(DbSchema::kDeviceTokensPersonId)));
    outNewUuid = row.at(std::string(DbSchema::kDeviceTokensUuid));
    return true;
}

void DeviceTokens::RemoveDeviceTokenById(Transaction& transaction, int64_t id) {
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kDeviceTokensTable,
        DbSchema::kDeviceTokensId,
        StringFromInt(id));
}

void DeviceTokens::RemoveDeviceTokensForUser(Transaction& transaction, int64_t personId) {
    KeyValueTable where = {
        { std::string(DbSchema::kDeviceTokensPersonId), StringFromInt(personId) }
    };
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kDeviceTokensTable,
        where);
}

int64_t DeviceTokens::DeleteExpired(Transaction& transaction, int64_t asOfUs) {
    KeyValueTableArray deleted = transaction.RunSqlStatementReturningKeyValueTableArray(
        "DELETE FROM device_tokens WHERE expires_at < $1 RETURNING id",
        StringFromInt(asOfUs));
    return static_cast<int64_t>(deleted.size());
}

} // namespace TableHelpers