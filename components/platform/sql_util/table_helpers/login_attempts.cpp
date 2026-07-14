#include "sql_util/table_helpers/login_attempts.h"

#include <stdexcept>

#include "db_schema/login_attempts.h"

namespace TableHelpers {

LoginAttempts::LoginAttempts(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

void LoginAttempts::RecordAttempt(
    Transaction& transaction,
    std::string_view emailLower,
    std::string_view ip,
    std::string_view kind,
    bool success) {
    // attempted_at = now_us() — taken at insert time so write-behind
    // latency doesn't backdate the row. The async recorder enqueues
    // the write via ThreadPool, so the request handler returns
    // before this insert lands.
    transaction.RunSqlStatement(
        "INSERT INTO login_attempts "
        "(email_lower, ip, attempted_at, success, kind) "
        "VALUES ($1, $2, now_us(), $3, $4)",
        std::string(emailLower),
        std::string(ip),
        success ? std::string("t") : std::string("f"),
        std::string(kind));
}

int64_t LoginAttempts::RecentFailureCountForEmail(
    Transaction& transaction,
    std::string_view emailLower,
    std::string_view kind,
    int64_t windowMicros) {
    if (windowMicros <= 0) return 0;
    std::string sql =
        "SELECT count(*) FROM login_attempts "
        "WHERE email_lower = $1 AND kind = $2 AND success = false "
        "AND attempted_at >= now_us() - $3::bigint";
    std::string countStr = transaction.RunSqlStatementReturningOneValue(
        sql,
        std::string(emailLower),
        std::string(kind),
        std::to_string(windowMicros));
    try {
        return std::stoll(countStr);
    } catch (...) {
        return 0;
    }
}

int64_t LoginAttempts::RecentFailureCountForIp(
    Transaction& transaction,
    std::string_view ip,
    std::string_view kind,
    int64_t windowMicros) {
    if (windowMicros <= 0) return 0;
    std::string sql =
        "SELECT count(*) FROM login_attempts "
        "WHERE ip = $1 AND kind = $2 AND success = false "
        "AND attempted_at >= now_us() - $3::bigint";
    std::string countStr = transaction.RunSqlStatementReturningOneValue(
        sql,
        std::string(ip),
        std::string(kind),
        std::to_string(windowMicros));
    try {
        return std::stoll(countStr);
    } catch (...) {
        return 0;
    }
}

int64_t LoginAttempts::PurgeOlderThan(
    Transaction& transaction,
    int64_t ageMicros) {
    if (ageMicros <= 0) return 0;
    std::string sql =
        "WITH deleted AS ("
        "  DELETE FROM login_attempts "
        "  WHERE attempted_at < now_us() - $1::bigint "
        "  RETURNING 1"
        ") SELECT count(*) FROM deleted";
    std::string countStr = transaction.RunSqlStatementReturningOneValue(
        sql, std::to_string(ageMicros));
    try {
        return std::stoll(countStr);
    } catch (...) {
        return 0;
    }
}

}  // namespace TableHelpers
