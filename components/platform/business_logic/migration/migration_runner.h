#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/transaction.h"
#include "sql_util/database_access/transaction_provider.h"
#include "sql_util/table_helpers/schema_migrations.h"

namespace Migration {

// A single schema migration. The `id` is what gets recorded in
// schema_migrations so subsequent runs know to skip it. Ids should be
// monotonically ordered strings — convention is "NNNN_short_name", e.g.
// "0001_baseline", "0002_add_subscription_tier".
//
// `apply` is the callback that performs the actual schema change. It runs
// inside a transaction managed by the MigrationRunner; on throw, the
// transaction rolls back and the migration is treated as not applied.
struct Migration {
    std::string id;
    std::function<void(Transaction&)> apply;
};

// Outcome of an ApplyPending call. `applied` lists ids that the runner
// just inserted into schema_migrations during this invocation; `skipped`
// lists ids that were already there. Returned in execution order.
struct MigrationResult {
    std::vector<std::string> applied;
    std::vector<std::string> skipped;
};

// Thrown when a migration's `apply` callback throws. Wraps the original
// exception message with the failing migration's id for log/observability.
// Migrations earlier in the list are committed (each runs in its own
// transaction); the migration with this id and everything after it are
// NOT applied.
class MigrationFailure : public std::runtime_error {
public:
    MigrationFailure(std::string_view migrationId, const std::string& innerWhat);
    const std::string& MigrationId() const { return migrationId_; }
private:
    std::string migrationId_;
};

// Applies pending migrations in order, delegating all schema_migrations
// reads/writes to `TableHelpers::SchemaMigrations`. No SQL lives in this
// class — it is pure orchestration.
//
// Production usage:
//   MigrationRunner runner(databaseHelper);
//   runner.ApplyPending(*transactionProvider, BuildAllMigrations());
//
// Each migration runs in its own transaction via the supplied provider —
// so a failure at migration N leaves migrations 1..N-1 committed and
// migrations N..end unattempted. This matches Rails/Django/Flyway
// semantics.
//
// The schema_migrations table itself is created by the normal
// `MakeDatabaseInfo` + `CreateTables` flow in `knottyyoga_database_helper`.
// This runner does NOT bootstrap it — there is no prior production state
// to defend against.
class MigrationRunner {
public:
    explicit MigrationRunner(DatabaseHelper databaseHelper);

    // True if `id` is recorded as applied in schema_migrations.
    bool IsApplied(Transaction& transaction, std::string_view id);

    // Returns all applied migration ids in chronological order.
    std::vector<std::string> ListApplied(Transaction& transaction);

    // Applies one migration if not already applied:
    //   1. If IsApplied(id): return false (skipped).
    //   2. Call migration.apply(transaction).
    //   3. Record id in schema_migrations.
    // Returns true if applied, false if skipped. If apply() throws, the
    // exception propagates and step 3 is never reached.
    bool ApplyOne(Transaction& transaction, const Migration& migration);

    // Apply every migration whose id isn't already in schema_migrations.
    // Each migration runs in its own RunInTransaction call so a failure
    // mid-list doesn't roll back earlier successes.
    //
    // If a migration's apply() throws, this method re-throws a
    // MigrationFailure naming the id; subsequent migrations are NOT
    // attempted.
    MigrationResult ApplyPending(
        TransactionProvider& transactionProvider,
        const std::vector<Migration>& migrations);

private:
    TableHelpers::SchemaMigrations schemaMigrations_;
};

}  // namespace Migration
