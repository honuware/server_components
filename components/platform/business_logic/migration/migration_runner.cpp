#include "migration_runner.h"

#include <utility>

#include "util/logging.h"

namespace Migration {

MigrationFailure::MigrationFailure(
    std::string_view migrationId, const std::string& innerWhat)
    : std::runtime_error(
          "Migration '" + std::string(migrationId) + "' failed: " + innerWhat),
      migrationId_(migrationId) {}

MigrationRunner::MigrationRunner(DatabaseHelper databaseHelper)
    : schemaMigrations_(std::move(databaseHelper)) {}

bool MigrationRunner::IsApplied(
    Transaction& transaction, std::string_view id) {
    return schemaMigrations_.IsApplied(transaction, id);
}

std::vector<std::string> MigrationRunner::ListApplied(
    Transaction& transaction) {
    return schemaMigrations_.ListAppliedIds(transaction);
}

bool MigrationRunner::ApplyOne(
    Transaction& transaction, const Migration& migration) {
    if (schemaMigrations_.IsApplied(transaction, migration.id)) {
        return false;
    }
    migration.apply(transaction);
    schemaMigrations_.RecordApplied(transaction, migration.id);
    return true;
}

MigrationResult MigrationRunner::ApplyPending(
    TransactionProvider& transactionProvider,
    const std::vector<Migration>& migrations) {
    MigrationResult result;
    for (const auto& migration : migrations) {
        bool applied = false;
        try {
            transactionProvider.RunInTransaction([&](Transaction& transaction) {
                applied = ApplyOne(transaction, migration);
            });
        } catch (const std::exception& e) {
            LogError() << "[migration] event=apply_failed id="
                       << migration.id
                       << " error=\"" << e.what() << "\"\n";
            throw MigrationFailure(migration.id, e.what());
        } catch (...) {
            LogError() << "[migration] event=apply_failed id="
                       << migration.id
                       << " error=\"unknown exception\"\n";
            throw MigrationFailure(migration.id, "unknown exception");
        }
        if (applied) {
            LogInfo() << "[migration] event=applied id="
                      << migration.id << "\n";
            result.applied.push_back(migration.id);
        } else {
            LogInfo() << "[migration] event=skipped id="
                      << migration.id << " reason=already_applied\n";
            result.skipped.push_back(migration.id);
        }
    }
    return result;
}

}  // namespace Migration
