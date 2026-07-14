#pragma once

#include <string>

#include "sql_util/database_access/transaction.h"

namespace Extensions {

// Loads every Postgres extension the schema relies on (citext, …). Call this
// before any DDL that references one of those types. Idempotent — uses
// `CREATE EXTENSION IF NOT EXISTS` under the hood, so it's safe to invoke on
// every database bootstrap.
//
// Production path: invoked from `database_helper/create_database.cpp` before
// `CreateStoredProceduresBeforeTables` and `CreateTables`.
//
// Test path: the SQL string is concatenated into the batch built by
// `GlobalDatabaseTestSupport::SetupAllTables` so test databases pick up the
// same extensions automatically.
//
// Phase 1.6 of the security review.
void CreateExtensions(Transaction& transaction);

// Returns the same DDL as CreateExtensions in textual form, intended for
// callers that batch DDL into a single round-trip (the test bootstrap is the
// canonical use). The trailing newline is included so callers can append more
// DDL without worrying about delimiters.
std::string GenerateCreateExtensionsSql();

}  // namespace Extensions
