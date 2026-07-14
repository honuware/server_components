#pragma once

#include <vector>

#include "business_logic/migration/migration_runner.h"

namespace Migration {

// The honuware **framework** migration stream. In the Phase 2.3 component split
// this builder ships with `honuware_platform` (it evolves honuware's own
// framework tables — people/sessions/roles/permissions/config_secrets/photos/
// admin metadata/etc.). Every migration it returns is recorded under the
// `kFrameworkMigrationNamespace` id namespace (see NamespacedMigrationId), so
// it can never collide with an app migration. It is applied *before* the app
// stream — see BuildAllMigrations.
//
// **Currently empty** — the fresh-install framework schema is built by
// MakeDatabaseInfo + CreateTables; framework migrations are added here only to
// evolve honuware's tables between released versions. When you add one:
//   1. Pick the next framework local id ("0001_...", "0002_...").
//   2. Append `{ NamespacedMigrationId(kFrameworkMigrationNamespace, "0001_..."), apply }`.
std::vector<Migration> BuildFrameworkMigrations();

}  // namespace Migration
