#pragma once

#include <string>
#include <string_view>

namespace Migration {

// Migrations live in two disjoint id namespaces so honuware *framework*
// migrations and *app* migrations can never collide in the schema_migrations
// table — even if the two streams independently pick the same local number.
//
// The tenant plan applies both streams to every tenant database in a loop
// (componentization Phase 2.3, `⇦ tenancy`): namespacing is what keeps a
// honuware framework upgrade ("0002_add_auth_index") from clobbering an
// unrelated app migration that also happens to be numbered "0002".
//
// The recorded schema_migrations id is "<namespace>/<localId>", e.g.
// "honuware/0001_baseline" or "app/0003_add_loyalty_tier". Local ids keep the
// existing "NNNN_snake_case" convention and never contain '/', so the composed
// id round-trips to (namespace, localId) unambiguously.
inline constexpr std::string_view kFrameworkMigrationNamespace = "honuware";
inline constexpr std::string_view kAppMigrationNamespace = "app";

// Separator between namespace and local id — chosen because it cannot appear
// in a local id.
inline constexpr char kMigrationNamespaceSeparator = '/';

// Builds the fully-namespaced schema_migrations id "<namespace>/<localId>".
std::string NamespacedMigrationId(
    std::string_view migrationNamespace, std::string_view localId);

}  // namespace Migration
