#include "admin_alerts.h"

#include "db_schema/admin_alerts.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/database_access/database_util.h"

namespace TableHelpers {

namespace {

constexpr std::string_view kCallGetAdminAlertsInWindowSql = R"SQL(
SELECT * FROM get_admin_alerts_in_window($1);
)SQL";

}  // namespace {

AdminAlerts::AdminAlerts(DatabaseHelper databaseHelper) : databaseHelper_(databaseHelper) {}

void AdminAlerts::AddAdminAlert(
    Transaction& transaction, std::string_view description) {
    KeyValueTable keyValueTable = MakeKeyValueTable(
        { {DbSchema::kAdminAlertsDescription, description} });
    DbCrud::AddRowToTable(
        transaction,
        databaseHelper_,
        DbSchema::kAdminAlertsTable,
        keyValueTable);
}

void AdminAlerts::DeleteAdminAlert(Transaction& transaction, int64_t alertId) {
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kAdminAlertsTable,
        DbSchema::kAdminAlertsId,
        std::to_string(alertId));
}

KeyValueTableArray AdminAlerts::GetAdminAlerts(Transaction& transaction, int64_t microsAlertWindow) {
    return transaction.RunSqlStatementReturningKeyValueTableArray(
        kCallGetAdminAlertsInWindowSql,
        std::to_string(microsAlertWindow));
}

KeyValueTableArray AdminAlerts::GetAdminAlertsSince(
    Transaction& transaction, int64_t sinceUs) {
    if (sinceUs < 0) sinceUs = 0;
    return transaction.RunSqlStatementReturningKeyValueTableArray(
        "SELECT id, created_at, description FROM admin_alerts "
        "WHERE created_at > $1::bigint "
        "ORDER BY created_at ASC",
        std::to_string(sinceUs));
}

}  // namespace TableHelpers
