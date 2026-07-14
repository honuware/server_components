#include "sql_util/table_helpers/auth_events.h"

#include <stdexcept>

#include "db_schema/auth_events.h"

namespace TableHelpers {

AuthEvents::AuthEvents(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

void AuthEvents::RecordEvent(
    Transaction& transaction,
    std::string_view kind,
    int64_t personId,
    std::string_view ip,
    std::string_view userAgent,
    std::string_view detailJson) {
    if (personId > 0) {
        transaction.RunSqlStatement(
            "INSERT INTO auth_events "
            "(kind, person_id, ip, user_agent, when_us, detail_json) "
            "VALUES ($1, $2, $3, $4, now_us(), $5)",
            std::string(kind),
            std::to_string(personId),
            std::string(ip),
            std::string(userAgent),
            std::string(detailJson));
    } else {
        // Explicit NULL on person_id for the unknown-person case
        // (login_failure on unrecognised email).
        transaction.RunSqlStatement(
            "INSERT INTO auth_events "
            "(kind, person_id, ip, user_agent, when_us, detail_json) "
            "VALUES ($1, NULL, $2, $3, now_us(), $4)",
            std::string(kind),
            std::string(ip),
            std::string(userAgent),
            std::string(detailJson));
    }
}

int64_t AuthEvents::CountRecentByKind(
    Transaction& transaction,
    std::string_view kind,
    int64_t windowMicros) {
    if (windowMicros <= 0) return 0;
    std::string countStr = transaction.RunSqlStatementReturningOneValue(
        "SELECT count(*) FROM auth_events "
        "WHERE kind = $1 AND when_us >= now_us() - $2::bigint",
        std::string(kind),
        std::to_string(windowMicros));
    try {
        return std::stoll(countStr);
    } catch (...) {
        return 0;
    }
}

KeyValueTableArray AuthEvents::RecentByPersonAndKind(
    Transaction& transaction,
    int64_t personId,
    std::string_view kind,
    int64_t windowMicros) {
    if (windowMicros <= 0 || personId <= 0) return {};
    return transaction.RunSqlStatementReturningKeyValueTableArray(
        "SELECT id, kind, person_id, ip, user_agent, when_us, detail_json "
        "FROM auth_events "
        "WHERE person_id = $1 AND kind = $2 "
        "AND when_us >= now_us() - $3::bigint "
        "ORDER BY when_us DESC",
        std::to_string(personId),
        std::string(kind),
        std::to_string(windowMicros));
}

}  // namespace TableHelpers
