#pragma once

#include <vector>

#include "business_logic/migration/migration_runner.h"
#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/transaction_provider.h"

namespace Migration {

// Thin orchestration wrapper for the `--migrate` mode of
// `knottyyoga_database_helper`:
//   1. Runs `MigrationRunner::ApplyPending` against the supplied list.
//   2. Logs a single structured success or failure line.
//   3. Returns an exit code suitable for `main` to forward to the OS:
//      - 0 on success (zero or more migrations applied/skipped cleanly).
//      - 1 on `MigrationFailure` (a migration's apply() threw).
//      - 1 on any other exception escaping the runner.
//
// Takes the migration list as a parameter rather than calling
// `BuildAllMigrations` internally so tests can pass arbitrary fixtures
// without depending on the current state of the project's actual list.
int RunMigrateCommand(
    TransactionProvider& transactionProvider,
    DatabaseHelper databaseHelper,
    const std::vector<Migration>& migrations);

}  // namespace Migration
