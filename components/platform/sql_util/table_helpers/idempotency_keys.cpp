#include "idempotency_keys.h"

#include "db_schema/idempotency_keys.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

namespace {

constexpr std::string_view kSqlGetIdempotencyKeysByScope =
    "SELECT *, COUNT(*) OVER() AS _total_count FROM idempotency_keys "
    "WHERE scope = $1 "
    "ORDER BY created_us DESC, id ASC "
    "LIMIT NULLIF($2, 0) OFFSET $3";

constexpr std::string_view kSqlGetIdempotencyKeysByStatus =
    "SELECT *, COUNT(*) OVER() AS _total_count FROM idempotency_keys "
    "WHERE status = $1 "
    "ORDER BY created_us DESC, id ASC "
    "LIMIT NULLIF($2, 0) OFFSET $3";

constexpr std::string_view kSqlGetExpiredIdempotencyKeys =
    "SELECT *, COUNT(*) OVER() AS _total_count FROM idempotency_keys "
    "WHERE expires_us < $1 "
    "ORDER BY expires_us ASC, id ASC "
    "LIMIT NULLIF($2, 0) OFFSET $3";

constexpr std::string_view kSqlDeleteExpiredIdempotencyKeys =
    "DELETE FROM idempotency_keys WHERE expires_us < $1 RETURNING id";

void ExtractTotalCount(KeyValueTableArray& results, int64_t* totalCount) {
    if (!results.empty()) {
        if (totalCount != nullptr) {
            *totalCount = std::stoll(results[0]["_total_count"]);
        }
        for (auto& row : results) {
            row.erase("_total_count");
        }
    } else if (totalCount != nullptr) {
        *totalCount = 0;
    }
}

} // namespace

IdempotencyKeys::IdempotencyKeys(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

int64_t IdempotencyKeys::AddIdempotencyKey(
    Transaction& transaction,
    std::string_view scope,
    std::string_view key,
    std::string_view endpoint,
    std::string_view requestHash,
    std::string_view status,
    std::string_view responseJson,
    int64_t expiresUs) {
    KeyValueTable kv = {
        { std::string(DbSchema::kIdempotencyKeysScope), std::string(scope) },
        { std::string(DbSchema::kIdempotencyKeysKey), std::string(key) },
        { std::string(DbSchema::kIdempotencyKeysEndpoint), std::string(endpoint) },
        { std::string(DbSchema::kIdempotencyKeysStatus), std::string(status) },
        { std::string(DbSchema::kIdempotencyKeysExpiresUs), StringFromInt(expiresUs) }
    };
    if (!requestHash.empty()) {
        kv[std::string(DbSchema::kIdempotencyKeysRequestHash)] = std::string(requestHash);
    }
    if (!responseJson.empty()) {
        kv[std::string(DbSchema::kIdempotencyKeysResponseJson)] = std::string(responseJson);
    }
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        databaseHelper_,
        DbSchema::kIdempotencyKeysTable,
        kv);
}

KeyValueTable IdempotencyKeys::GetIdempotencyKey(Transaction& transaction, int64_t id) const {
    return DbCrud::GetRow(
        transaction,
        databaseHelper_,
        DbSchema::kIdempotencyKeysTable,
        DbSchema::kIdempotencyKeysId,
        StringFromInt(id));
}

KeyValueTable IdempotencyKeys::GetIdempotencyKeyByScopeKeyEndpoint(
    Transaction& transaction,
    std::string_view scope,
    std::string_view key,
    std::string_view endpoint) const {
    return DbCrud::LookupRowByValues(
        transaction,
        databaseHelper_,
        DbSchema::kIdempotencyKeysTable,
        {
            { std::string(DbSchema::kIdempotencyKeysScope), std::string(scope) },
            { std::string(DbSchema::kIdempotencyKeysKey), std::string(key) },
            { std::string(DbSchema::kIdempotencyKeysEndpoint), std::string(endpoint) }
        });
}

KeyValueTableArray IdempotencyKeys::GetIdempotencyKeysByScope(
    Transaction& transaction,
    std::string_view scope,
    int64_t pageSize,
    int64_t pageOffset,
    int64_t* totalCount) const {
    KeyValueTableArray results = transaction.RunSqlStatementReturningKeyValueTableArray(
        std::string(kSqlGetIdempotencyKeysByScope),
        std::string(scope), StringFromInt(pageSize), StringFromInt(pageOffset));

    ExtractTotalCount(results, totalCount);
    return results;
}

KeyValueTableArray IdempotencyKeys::GetIdempotencyKeysByStatus(
    Transaction& transaction,
    std::string_view status,
    int64_t pageSize,
    int64_t pageOffset,
    int64_t* totalCount) const {
    KeyValueTableArray results = transaction.RunSqlStatementReturningKeyValueTableArray(
        std::string(kSqlGetIdempotencyKeysByStatus),
        std::string(status), StringFromInt(pageSize), StringFromInt(pageOffset));

    ExtractTotalCount(results, totalCount);
    return results;
}

KeyValueTableArray IdempotencyKeys::GetExpiredIdempotencyKeys(
    Transaction& transaction,
    int64_t currentTimeUs,
    int64_t pageSize,
    int64_t pageOffset,
    int64_t* totalCount) const {
    KeyValueTableArray results = transaction.RunSqlStatementReturningKeyValueTableArray(
        std::string(kSqlGetExpiredIdempotencyKeys),
        StringFromInt(currentTimeUs), StringFromInt(pageSize), StringFromInt(pageOffset));

    ExtractTotalCount(results, totalCount);
    return results;
}

void IdempotencyKeys::SetStatus(Transaction& transaction, int64_t id, std::string_view status) {
    KeyValueTable kv = {
        { std::string(DbSchema::kIdempotencyKeysStatus), std::string(status) }
    };
    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kIdempotencyKeysTable,
        DbSchema::kIdempotencyKeysId,
        StringFromInt(id),
        kv,
        {});
}

void IdempotencyKeys::SetStatusAndResponse(
    Transaction& transaction,
    int64_t id,
    std::string_view status,
    std::string_view responseJson) {
    KeyValueTable kv = {
        { std::string(DbSchema::kIdempotencyKeysStatus), std::string(status) }
    };
    StringSet allowedSql = {};

    if (!responseJson.empty()) {
        kv[std::string(DbSchema::kIdempotencyKeysResponseJson)] = std::string(responseJson);
    } else {
        kv[std::string(DbSchema::kIdempotencyKeysResponseJson)] = "NULL";
        allowedSql.insert("NULL");
    }

    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kIdempotencyKeysTable,
        DbSchema::kIdempotencyKeysId,
        StringFromInt(id),
        kv,
        allowedSql);
}

void IdempotencyKeys::DeleteIdempotencyKey(Transaction& transaction, int64_t id) {
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kIdempotencyKeysTable,
        DbSchema::kIdempotencyKeysId,
        StringFromInt(id));
}

int64_t IdempotencyKeys::DeleteExpiredIdempotencyKeys(Transaction& transaction, int64_t currentTimeUs) {
    KeyValueTableArray deleted = transaction.RunSqlStatementReturningKeyValueTableArray(
        std::string(kSqlDeleteExpiredIdempotencyKeys),
        StringFromInt(currentTimeUs));
    return static_cast<int64_t>(deleted.size());
}

} // namespace TableHelpers
