#pragma once

#include <optional>
#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class IdempotencyKeys {
public:
    explicit IdempotencyKeys(DatabaseHelper databaseHelper);
    IdempotencyKeys(const IdempotencyKeys&) = default;
    IdempotencyKeys& operator=(const IdempotencyKeys&) = default;
    ~IdempotencyKeys() = default;

    // CREATE
    int64_t AddIdempotencyKey(
        Transaction& transaction,
        std::string_view scope,
        std::string_view key,
        std::string_view endpoint,
        std::string_view requestHash,
        std::string_view status,
        std::string_view responseJson,
        int64_t expiresUs);

    // READ
    KeyValueTable GetIdempotencyKey(Transaction& transaction, int64_t id) const;
    KeyValueTable GetIdempotencyKeyByScopeKeyEndpoint(
        Transaction& transaction,
        std::string_view scope,
        std::string_view key,
        std::string_view endpoint) const;
    KeyValueTableArray GetIdempotencyKeysByScope(
        Transaction& transaction,
        std::string_view scope,
        int64_t pageSize = 0,
        int64_t pageOffset = 0,
        int64_t* totalCount = nullptr) const;
    KeyValueTableArray GetIdempotencyKeysByStatus(
        Transaction& transaction,
        std::string_view status,
        int64_t pageSize = 0,
        int64_t pageOffset = 0,
        int64_t* totalCount = nullptr) const;
    KeyValueTableArray GetExpiredIdempotencyKeys(
        Transaction& transaction,
        int64_t currentTimeUs,
        int64_t pageSize = 0,
        int64_t pageOffset = 0,
        int64_t* totalCount = nullptr) const;

    // UPDATE
    void SetStatus(Transaction& transaction, int64_t id, std::string_view status);
    void SetStatusAndResponse(
        Transaction& transaction,
        int64_t id,
        std::string_view status,
        std::string_view responseJson);

    // DELETE
    void DeleteIdempotencyKey(Transaction& transaction, int64_t id);

    // Deletes all idempotency keys whose expires_us is strictly less than currentTimeUs.
    // Returns the count of rows deleted.
    int64_t DeleteExpiredIdempotencyKeys(Transaction& transaction, int64_t currentTimeUs);

private:
    DatabaseHelper databaseHelper_;
};

} // namespace TableHelpers
