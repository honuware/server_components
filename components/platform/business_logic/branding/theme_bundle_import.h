#pragma once

#include <string>

#include "business_logic/branding/theme_bundle.h"
#include "business_logic/branding/theme_bundle_migrations.h"
#include "sql_util/database_access/database_helper.h"
#include "util/secrets/secrets_helper.h"

namespace Branding {

// Tenant Theming Phase 9 — a ThemeBundle -> the tenant's database.
//
// Two contracts worth stating plainly, because they are the ones a caller can
// get wrong:
//
// REPLACE IS THE DEFAULT (OQ-TF2). A registered slot or token ABSENT from the
// bundle is reset to its default, not left at the previous theme's value.
// Without that, flipping between two themes accumulates the union of both and
// neither look is what its author intended — which would defeat the entire
// point of being able to try alternatives. `merge` is the explicit opt-in for
// "apply just these on top of what is there".
//
// VALIDATE EVERYTHING, THEN WRITE. A half-applied theme is worse than a refused
// one: the studio is left with a site that is neither look and no way back.
// Every check runs before the first write, and the caller runs the whole thing
// in one transaction.

struct ThemeBundleImportOptions {
    BundleStrictness strictness = BundleStrictness::Strict;
    bool merge = false;
    // When true, nothing is written — the report says what WOULD happen. This
    // is what makes trying a theme safe: you see the consequences first.
    bool dryRun = false;
};

// Parses, migrates, validates and (unless dryRun) applies `json` plus `assets`.
//
// Never throws for bad input: a malformed bundle produces `ok == false` and a
// reason a studio can act on. The caller turns that into a 400 rather than a
// 500 — an admin's mis-typed file is not a server error.
BundleImportReport ImportThemeBundleJson(
    DatabaseHelper databaseHelper,
    Transaction& transaction,
    Secrets::SecretsHelper& secrets,
    const Json::Value& json,
    const std::map<std::string, std::string>& assets,
    const ThemeBundleImportOptions& options);

// Validation only, exposed for its own tests and for callers that already hold
// a parsed bundle. Returns "" when the bundle is internally consistent:
// every asset reference resolves, every enum is known, every value validates.
std::string ValidateThemeBundle(
    const ThemeBundle& bundle,
    BundleStrictness strictness,
    std::vector<std::string>& unknownKeysOut);

}  // namespace Branding
