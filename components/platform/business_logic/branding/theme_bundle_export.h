#pragma once

#include <string>

#include "business_logic/branding/theme_bundle.h"
#include "sql_util/database_access/database_helper.h"
#include "util/secrets/secrets_helper.h"

namespace Branding {

// Tenant Theming Phase 9 — the tenant's database -> a ThemeBundle.
//
// Reads ONLY the allow-listed `site_*` keys, the three font tables, and each
// registered app section. `config_secrets` also holds Square tokens and the
// SMTP password, so this is an allow-list by construction: a key that is not in
// a registry cannot reach a bundle even by accident.
//
// Asset names are DERIVED, deterministically, from what the asset is — the same
// photo produces the same filename on every export. That is not tidiness: the
// acceptance test for this whole feature is that export -> import -> export is
// byte-identical, and a name that varied per run would break it.

struct ThemeBundleExportOptions {
    // Goes into `name` / `description` in the file. Neither affects import.
    std::string name;
    std::string description;
    // Provenance for a human only. `exportedAt` is passed in rather than read
    // from a clock so a test can produce a stable bundle.
    std::string exportedAt;
    std::string app;
    std::string site;
    std::string honuwareVersion;
};

// Fills `out`. Returns "" on success, or a reason.
//
// Failure is all-or-nothing: a bundle that is missing a font file it references
// would import into a broken look, so a section or asset that cannot be read
// fails the export rather than being quietly omitted.
std::string ExportThemeBundle(
    DatabaseHelper databaseHelper,
    Transaction& transaction,
    Secrets::SecretsHelper& secrets,
    const ThemeBundleExportOptions& options,
    ThemeBundle& out);

}  // namespace Branding
