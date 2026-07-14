#include "migrate_command.h"

#include <exception>

#include "util/logging.h"

namespace Migration {

int RunMigrateCommand(
    TransactionProvider& transactionProvider,
    DatabaseHelper databaseHelper,
    const std::vector<Migration>& migrations) {
    MigrationRunner runner(databaseHelper);
    try {
        MigrationResult result = runner.ApplyPending(
            transactionProvider, migrations);
        LogInfo() << "[migrate] event=success"
                  << " applied=" << result.applied.size()
                  << " skipped=" << result.skipped.size() << "\n";
        return 0;
    } catch (const MigrationFailure& e) {
        // ApplyPending already logged the per-migration failure; this is
        // the command-level summary so the operator sees a single
        // event=failure line at the end of the journal.
        LogError() << "[migrate] event=failure id=" << e.MigrationId()
                   << " error=\"" << e.what() << "\"\n";
        return 1;
    } catch (const std::exception& e) {
        // Defensive: a non-MigrationFailure thrown from within ApplyPending
        // would normally already be wrapped, but if anything escapes
        // (logging error, transaction provider failure, etc.) we still
        // return a clean exit code rather than aborting the process.
        LogError() << "[migrate] event=failure error=\"" << e.what() << "\"\n";
        return 1;
    }
}

}  // namespace Migration
