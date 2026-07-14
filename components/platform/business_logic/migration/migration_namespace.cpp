#include "migration_namespace.h"

namespace Migration {

std::string NamespacedMigrationId(
    std::string_view migrationNamespace, std::string_view localId) {
    std::string id;
    id.reserve(migrationNamespace.size() + 1 + localId.size());
    id.append(migrationNamespace);
    id.push_back(kMigrationNamespaceSeparator);
    id.append(localId);
    return id;
}

}  // namespace Migration
