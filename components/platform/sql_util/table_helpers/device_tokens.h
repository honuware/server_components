#pragma once

#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class DeviceTokens
{
public:
    DeviceTokens(DatabaseHelper databaseHelper);
    DeviceTokens(const DeviceTokens&) = default;
    DeviceTokens& operator=(const DeviceTokens&) = default;
    ~DeviceTokens() = default;

    int64_t AddDeviceToken(
        Transaction& transaction,
        int64_t personId,
        std::string_view secretHash,
        int64_t microsUntilExpires);

    KeyValueTable LookupDeviceTokenById(Transaction& transaction, int64_t id) const;
    KeyValueTable LookupDeviceTokenByUuid(Transaction& transaction, std::string_view uuid) const;
    KeyValueTableArray LookupDeviceTokensByPerson(Transaction& transaction, int64_t personId) const;
    KeyValueTable LookupDeviceTokenBySecretHash(Transaction& transaction, std::string_view secretHash) const;

    bool IsValid(Transaction& transaction, int64_t id) const;

    void Revoke(Transaction& transaction, int64_t id);

    // Returns true if token updated; false if token exists but is either expired or revoked.
    bool UpdateDeviceToken(
        Transaction& transaction,
        int64_t id,
        std::string_view secretHash,
        int64_t microsUntilExpires);

    // Atomically validate-and-rotate a device token. Phase 2.4 of the
    // security review: the old read+update pattern had a TOCTOU window
    // where two concurrent requests could both pass the validity check
    // before either UPDATE landed, mint two sessions off the same
    // device cookie, and leave one cookie in a revoked state without
    // the user being signed out. This single conditional UPDATE matches
    // a row by (oldSecretHash, oldUuid) — both pieces of the cookie —
    // gates on NOT revoked AND not expired, and rotates secret_hash,
    // uuid, last_used_at, expires_at in one statement.
    //
    // Returns true if the row was rotated. On true, outPersonId and
    // outNewUuid carry the row's person_id and the freshly-minted uuid
    // (the value the client should now hold). Returns false otherwise:
    // mismatched secret hash, mismatched uuid, revoked, expired, or
    // (defensively) a row that would otherwise be valid but didn't
    // match the WHERE clause.
    bool ConsumeAndRotate(
        Transaction& transaction,
        std::string_view oldSecretHash,
        std::string_view oldUuid,
        std::string_view newSecretHash,
        int64_t microsUntilExpires,
        int64_t& outPersonId,
        std::string& outNewUuid);

    void RemoveDeviceTokenById(Transaction& transaction, int64_t id);
    void RemoveDeviceTokensForUser(Transaction& transaction, int64_t personId);

    // Deletes all device tokens whose expires_at is strictly less than asOfUs.
    // Returns the count of rows deleted.
    int64_t DeleteExpired(Transaction& transaction, int64_t asOfUs);

private:
    DatabaseHelper databaseHelper_;
};

} // namespace TableHelpers