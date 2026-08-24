#pragma once

#include <functional>
#include <string>
#include <vector>

#include "util/json_value.h"

namespace Branding {

// Tenant Theming Phase 9 (OQ-TF3) — two mechanisms, deliberately separate,
// because they answer different questions.
//
//   MIGRATION  answers "this bundle was written by an older build".
//              An ordered chain rewrites the JSON forward before validation.
//   STRICTNESS answers "this bundle has a key I still do not recognise".
//
// Migration runs FIRST. That ordering is the whole point: a renamed token would
// otherwise turn every theme file ever exported into a pile of unknown keys
// overnight, and strictness would refuse bundles that are merely old rather
// than actually wrong.

// Bumped only when a change cannot be read by the previous reader. Additive
// fields do not bump it — an older reader ignores what it does not know, and an
// unknown SECTION is already handled by the app-section seam.
int CurrentBundleFormatVersion();

// The oldest `format_version` the chain can still migrate from.
int OldestSupportedBundleFormatVersion();

// One step. `apply` is a pure JSON rewrite: no database, no I/O, no clock — so
// it is testable in isolation and produces the same result forever.
struct BundleMigration {
    int from = 0;
    int to = 0;
    // One line, shown to the studio in the import report. "site_theme_brand →
    // site_theme_primary" is the useful kind; "fix stuff" is not.
    std::string description;
    std::function<void(Json::Value&)> apply;
};

// The chain, oldest first. Contiguity (each step's `to` is the next step's
// `from`, ending at the current version) is asserted by a test rather than
// discovered at runtime by a studio whose import failed.
const std::vector<BundleMigration>& BundleMigrations();

// Rewrites `json` forward from its own `format_version` to the current one.
//
// Returns "" on success and appends each applied step's description to
// `appliedOut`. Returns a reason when the document's version is newer than this
// build understands, or older than the chain reaches — a bundle from a newer
// build may mean things this one cannot honour, so it is refused rather than
// half-applied.
std::string MigrateBundleJson(
    Json::Value& json, std::vector<std::string>& appliedOut);

// How to treat a key that is still unrecognised after migration (OQ-TF3).
enum class BundleStrictness {
    // Refuse the import, naming every unknown key. The default: a typo'd token
    // silently doing nothing is the worst of the available outcomes.
    Strict,
    // Apply what is understood and REPORT what was skipped. Never silent — a
    // mode that hid what it dropped would be worse than either option.
    Lenient,
};

// One thing the import could not do, and why.
//
// The whole point is that a problem is ATTRIBUTED. "That theme file is not
// valid" tells a studio nothing; "font Brandon Grotesque: the file
// brandon.woff2 is not in that theme file" tells them exactly what to fix. The
// three fields are what a message needs to be actionable: where it happened,
// which thing it was, and what was wrong with it.
struct BundleProblem {
    // "assets", "fonts", "content", "tokens", or "section:page_content".
    std::string area;
    // The key, family name or file name this is about. May be empty when the
    // problem is about the area as a whole.
    std::string item;
    // Plain English, addressed to the studio — never a SQL error or a type name.
    std::string reason;
};

// What an import did, and what it declined to do. Returned by the dry-run
// endpoint as well as the real one, so a studio can see the consequences of a
// theme before committing to it.
struct BundleImportReport {
    bool ok = false;
    // 0 when the bundle was already current.
    int migratedFrom = 0;
    std::vector<std::string> migrationsApplied;
    // Keys that survived migration unrecognised. Non-empty + Strict = refused.
    std::vector<std::string> unknownKeys;
    // App sections present in the bundle that nothing has registered for.
    std::vector<std::string> skippedSections;
    // Everything that was SKIPPED rather than applied. Under Lenient this is
    // how a partly-understood theme reports itself: `ok` is still true, the
    // understood half is applied, and this says what was left out. Under Strict
    // the first entry becomes the refusal reason instead.
    //
    // An import that reports problems is NOT a failure — it is the normal way a
    // theme written by a different build, or against a database that lacks an
    // optional table, gets as far as it can.
    std::vector<BundleProblem> problems;
    int contentChanges = 0;
    int tokenChanges = 0;
    int fontFamilyChanges = 0;
    int assetChanges = 0;
    // Populated when ok == false.
    std::string error;
};

Json::Value BundleImportReportToJson(const BundleImportReport& report);

}  // namespace Branding
