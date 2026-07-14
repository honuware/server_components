#include "create_stored_procedures.h"

#include "get_admin_alerts_in_window.h"
#include "now_us.h"

namespace StoredProcedures {

void CreateStoredProceduresBeforeTables(Transaction& transaction) {
    CreateNowUs(transaction);
}
void CreateStoredProceduresAfterTables(Transaction& transaction) {
    CreateGetAdminAlertsInWindow(transaction);
}

std::string GenerateStoredProceduresBeforeTablesSql() {
    return GenerateNowUsSql();
}

std::string GenerateStoredProceduresAfterTablesSql() {
    return GenerateGetAdminAlertsInWindowSql();
}

} // namespace StoredProcedures

