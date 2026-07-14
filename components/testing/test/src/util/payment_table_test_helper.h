#pragma once

#include "sql_util/database_access/transaction.h"
#include "test/src/util/database_test_helper.h"

namespace TestUtil {

// No-op: all tables are pre-created at startup by SetupAllTables().
// These functions exist only for backward compatibility with test files
// that haven't removed the calls yet.
void MakePaymentTables(Transaction& transaction, TestDatabaseUtil& testDb);
void MakeSchedulingTables(Transaction& transaction, TestDatabaseUtil& testDb);

} // namespace TestUtil
