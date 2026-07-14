#include "sessions.h"

#include <stdexcept>
#include <sstream>

#include "db_schema/sessions.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/database_access/database_util.h"
#include "util/types.h"

namespace TableHelpers {

Sessions::Sessions(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

int64_t Sessions::AddSession(
    Transaction& transaction,
    int64_t personId,
    int64_t microsUntilExpires) {
    // Build SQL expression for expires_at = now_us + offset (microseconds)
    std::string expiresExpr =
        "(extract(epoch FROM now()) * 1000000)::bigint + " + StringFromInt(microsUntilExpires);

    KeyValueTable kv = {
        { std::string(DbSchema::kSessionsPersonId), StringFromInt(personId) },
        { std::string(DbSchema::kSessionsExpiresAt), expiresExpr }
    };
    // Allow our raw SQL expression to be inlined
    StringSet allowedSql = { expiresExpr };

    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        databaseHelper_,
        DbSchema::kSessionsTable,
        kv,
        allowedSql);
}

KeyValueTable Sessions::LookupSessionById(Transaction& transaction, int64_t id) const {
    KeyValueTable row = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kSessionsTable,
        DbSchema::kSessionsId,
        StringFromInt(id));
    if (row.empty()) {
        throw std::runtime_error("Sessions::LookupSessionById - not found");
    }
    return row;
}

KeyValueTable Sessions::LookupSessionByUuid(
    Transaction& transaction, std::string_view uuid) const {
    KeyValueTable row = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kSessionsTable,
        DbSchema::kSessionsUuid,
        uuid);
    if (row.empty()) {
        throw std::runtime_error("Sessions::LookupSessionByUuid - not found");
    }
    return row;
}


KeyValueTableArray Sessions::LookupSessionByPerson(
    Transaction& transaction, int64_t personId) const {
    KeyValueTable filter = {
        { std::string(DbSchema::kSessionsPersonId), StringFromInt(personId) }
    };
    KeyValueTableArray keyValueTableArray = DbCrud::GetRowsByValuesWithOrderBy(
        transaction,
        databaseHelper_,
        DbSchema::kSessionsTable,
        filter,
        DbSchema::kSessionsId,
        true);
    if (keyValueTableArray.empty()) {
        throw std::runtime_error("Sessions::LookupSessionByPerson - not found");
    }
    return keyValueTableArray;
}

void Sessions::UpdateLastSeen(Transaction& transaction, int64_t id) {
    // Ensure exists
    (void)LookupSessionById(transaction, id);

    std::string rowNowUs = "now_us()";

    KeyValueTable kv = {
        { std::string(DbSchema::kSessionsLastSeenAt), rowNowUs }
    };
    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kSessionsTable,
        DbSchema::kSessionsId,
        StringFromInt(id),
        kv,
        { rowNowUs });
}

void Sessions::RemoveSessionsForUser(Transaction& transaction, int64_t personId) {
    KeyValueTable where = {
        { std::string(DbSchema::kSessionsPersonId), StringFromInt(personId) }
    };
    // This delete should be a no-op if nothing matches.
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kSessionsTable,
        where);
}

void Sessions::RemoveSession(Transaction& transaction, int64_t sessionId) {
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kSessionsTable,
        DbSchema::kSessionsId,
        StringFromInt(sessionId));
}

} // namespace TableHelpers