#include "framework_migrations.h"

#include "migration_namespace.h"

namespace Migration {

std::vector<Migration> BuildFrameworkMigrations() {
    // Intentionally empty pre-deploy. Framework (honuware) schema changes that
    // must run against an already-provisioned database are appended here, each
    // id built with NamespacedMigrationId(kFrameworkMigrationNamespace,
    // "NNNN_name") so it stays in the honuware id namespace.
    return {};
}

}  // namespace Migration
