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
// DECIDE EVERYTHING, THEN WRITE. Every check — including which tables this
// database actually has — runs before the first write, and the caller runs the
// whole thing in one transaction. That ordering is not stylistic: a Postgres
// error mid-apply poisons the transaction, so anything that could throw has to
// be discovered while there is still something useful to do about it.
//
// TOLERANT BY ITEM, FATAL BY FILE. Only problems that make the bundle unusable
// (or that are security boundaries) refuse it. Everything else is dropped from
// the bundle, applied around, and reported — a theme with one bad font and one
// missing image restores everything else rather than nothing.

struct ThemeBundleImportOptions {
    // Strict is the LIBRARY default — a caller that says nothing gets
    // all-or-nothing, which is the safe thing for a programmatic import.
    //
    // The HTTP endpoint deliberately chooses Lenient instead: an administrator
    // restoring their own theme is better served by "here is your site back,
    // and here are the four things I could not place" than by a refusal they
    // cannot act on. See OptionsFrom in manage_site_theme_bundle.cpp.
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
//
// Kept as the STRICT, all-or-nothing check. `PruneUnusableBundleItems` is the
// tolerant path the importer actually takes; this remains for callers that want
// "is this file completely clean?" as a single question.
std::string ValidateThemeBundle(
    const ThemeBundle& bundle,
    BundleStrictness strictness,
    std::vector<std::string>& unknownKeysOut);

// Whole-file problems ONLY: the ones that make a bundle unusable however
// tolerant we are, plus the security boundaries.
//
// Deliberately short. An asset whose name is a path, a file that is neither an
// image nor a font, a bundle bigger than we accept, a `config_secrets` key
// outside the `site_` namespace — none of these can be "skipped and carried on
// from", because each is either meaningless or an attempt to use a theme file
// as something else. Everything else is a per-item problem.
std::string FindFatalBundleProblem(const ThemeBundle& bundle);

// Removes from `bundle` every item that cannot be applied, recording one
// BundleProblem for each, and returns the keys that are unrecognised but
// harmless.
//
// This is what makes an import tolerant: a theme with one bad font family and
// one dangling image reference applies everything else rather than refusing
// wholesale. A studio moving between builds — or importing a sibling app's
// theme — gets the parts that fit and a list of the parts that did not.
//
// Assumes FindFatalBundleProblem has already passed.
void PruneUnusableBundleItems(
    ThemeBundle& bundle,
    std::vector<std::string>& unknownKeysOut,
    std::vector<BundleProblem>& problemsOut);

}  // namespace Branding
