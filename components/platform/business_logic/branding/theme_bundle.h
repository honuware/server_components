#pragma once

#include <map>
#include <string>
#include <vector>

#include "util/json_value.h"

namespace Branding {

// Tenant Theming Phase 9 — a whole look as a portable artifact.
//
// A theme bundle is a DIRECTORY: `theme.json` plus every binary as a pathless
// sibling file. A `.zip` of that directory is the same bundle, for a browser
// that cannot be handed a folder.
//
// This header is the in-memory form both transports produce and both directions
// consume. It is pure data — no I/O, no SQL, no JSON — so the model can be
// exercised without a database and the conversions each live in one place:
//
//   theme_bundle_json     ThemeBundle <-> theme.json
//   theme_bundle_zip      ThemeBundle <-> a flat .zip
//   theme_bundle_export   the tenant's database -> ThemeBundle
//   theme_bundle_import   ThemeBundle -> the tenant's database
//
// What it deliberately does NOT carry: ids, timestamps, and anything outside the
// `site_*` allow-list. A theme is settings, not identity or history — see
// theme_bundle_import.h for why that boundary is load-bearing.

// The format's identity. An importer refuses a bundle whose `format` is not
// this, however well-formed the rest of it looks.
inline constexpr std::string_view kThemeBundleFormat = "honuware.site-theme";

// The one file that must exist in every bundle.
inline constexpr std::string_view kThemeBundleJsonName = "theme.json";

// The route a bundled image is served from once imported. A URL slot holding
// `logo.svg` in the file holds `/api/site_asset/logo.svg` in the database, so
// the stored value stays a servable URL exactly as it was before bundles
// existed — nothing downstream of the store had to learn a second form.
inline constexpr std::string_view kThemeBundleAssetUrlPrefix = "/api/site_asset/";

// ---- fonts -----------------------------------------------------------------

struct BundleFontPreconnect {
    std::string url;
    bool crossorigin = false;
};

struct BundleFontSource {
    std::string sourceKey;
    std::string displayName;
    std::string baseUrl;
    std::string querySuffix;
    // Structured rather than the stored `url|crossorigin` newline packing: the
    // packing is a storage detail of a single text column, and a theme file is
    // meant to be edited by a person.
    std::vector<BundleFontPreconnect> preconnects;
};

struct BundleFontFace {
    int weight = 400;
    std::string style;  // "normal" / "italic"
    // The asset filename this face's bytes live in. The format is re-derived
    // from the magic bytes on import — the extension here is never trusted.
    std::string file;
};

struct BundleFontFamily {
    std::string family;
    // D13: required, never assumed. A family with no fallback is a validation
    // error, not something to paper over with "sans-serif".
    std::string fallback;
    std::string sourceKind;  // cdn / uploaded / system
    std::string sourceKey;   // cdn only
    std::string spec;        // cdn only
    std::vector<BundleFontFace> faces;  // uploaded only
};

struct BundleFonts {
    std::vector<BundleFontSource> sources;
    // Order IS the ordinal. An explicit ordinal in a hand-edited file is one
    // more thing to keep consistent for no benefit.
    std::vector<BundleFontFamily> families;
};

// ---- the bundle ------------------------------------------------------------

struct ThemeBundle {
    // Envelope.
    int formatVersion = 0;
    std::string name;
    std::string description;
    std::string exportedAt;
    // Provenance for a human ONLY — never used to make a decision on import. A
    // theme from another app must still import; that is the point of the seam.
    std::string exportedFromApp;
    std::string exportedFromSite;
    std::string exportedFromHonuware;

    // `config_secrets` key -> value, for the registered content slots plus
    // site_logo_url. Values are in STORED form here (newline-packed `lines`,
    // `label|url` social links); theme_bundle_json owns the file-shape
    // conversion, so everything downstream of this struct speaks one dialect.
    std::map<std::string, std::string> content;

    // `config_secrets` key -> value for the registered theme tokens.
    std::map<std::string, std::string> tokens;

    BundleFonts fonts;

    // App-contributed sections, name -> the section's own JSON (e.g.
    // "page_content"). The framework carries these opaquely: it validates the
    // envelope and the assets they reference, and hands the body to whichever
    // section handler is registered. A bundle whose sections this app does not
    // know still imports its framework half.
    std::map<std::string, Json::Value> appSections;

    // filename -> raw bytes. Images and font faces both live here; there is one
    // namespace, so a logo and a font face cannot collide silently.
    std::map<std::string, std::string> assets;

    // Convenience for the many callers that only want to know whether an asset
    // is present before referencing it.
    bool HasAsset(const std::string& fileName) const {
        return assets.find(fileName) != assets.end();
    }
};

}  // namespace Branding
