#pragma once

#include <functional>
#include <string>
#include <vector>

#include "business_logic/branding/theme_bundle.h"
#include "sql_util/database_access/database_helper.h"

namespace Branding {

// Tenant Theming Phase 9 — the seam that lets an APP contribute a section to a
// framework-owned bundle format.
//
// `theme` and `fonts` are entirely honuware. `page_content` (home sections,
// Getting Started steps) is entirely knottyyoga — those are app tables. So the
// format cannot live wholly on either side, and this is the joint.
//
// The framework owns the envelope, the asset rules, the validation and the
// atomic apply; an app registers a named section and supplies the two halves of
// its own conversion. A bundle whose sections this app does NOT recognise still
// imports its framework half and reports what it skipped — which is what lets a
// CommunityFinder theme's colours and fonts be reused by Knotty Yoga even
// though CommunityFinder has no home sections.

struct SectionContext {
    // All three are pointers so the context is default-constructible: a section
    // handler can be unit-tested without standing up a database.
    DatabaseHelper* databaseHelper = nullptr;
    Transaction* transaction = nullptr;
    // A section adds its binaries here on export and reads them here on import.
    // One namespace with the framework's assets, so a home-section photo and a
    // font face cannot collide silently.
    ThemeBundle* bundle = nullptr;
};

// Export: fill `out` with this section's JSON and add any assets to the bundle.
// Return a reason to fail the whole export, or "" on success.
using SectionExporter =
    std::function<std::string(SectionContext& context, Json::Value& out)>;

// Import: apply `body` to the database. Runs inside the import's single
// transaction, AFTER the framework half, so a section may rely on fonts and
// tokens already being in place. Return a reason to fail the whole import, or
// "" on success.
//
// Called with a `merge` flag so a section can honour the same replace-vs-patch
// contract the framework half does.
using SectionImporter = std::function<std::string(
    SectionContext& context, const Json::Value& body, bool merge)>;

struct ThemeBundleSection {
    std::string name;
    SectionExporter exporter;
    SectionImporter importer;
};

// Registration is process-global and idempotent by name: registering the same
// name twice REPLACES the earlier entry rather than duplicating it, so a test
// can install a stub without leaking into the next test.
void RegisterThemeBundleSection(
    std::string name, SectionExporter exporter, SectionImporter importer);

// Every registered section, in registration order — which is the order they
// export and import in, so a bundle's section order is stable across runs.
const std::vector<ThemeBundleSection>& ThemeBundleSections();

// Nullptr when nothing has registered under this name. That is not an error:
// it is precisely the cross-app case, and the caller reports it as a skipped
// section rather than failing.
const ThemeBundleSection* FindThemeBundleSection(const std::string& name);

// Drops every registration. Tests only — production registers once at startup.
void ClearThemeBundleSectionsForTest();

}  // namespace Branding
