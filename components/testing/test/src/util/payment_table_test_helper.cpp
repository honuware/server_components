#include "payment_table_test_helper.h"

namespace TestUtil {

void MakePaymentTables(Transaction&, TestDatabaseUtil&) {
    // No-op: all tables are pre-created at startup by SetupAllTables().
}

void MakeSchedulingTables(Transaction&, TestDatabaseUtil&) {
    // No-op: all tables are pre-created at startup by SetupAllTables().
}

} // namespace TestUtil
