#pragma once

#include <string>

#include "business_logic/branding/theme_bundle.h"

namespace Branding {

// Tenant Theming Phase 9 (OQ-TF1) — the bundle as a single file, for the admin
// page. A browser cannot be handed a folder.
//
// The archive is FLAT: `theme.json` plus one entry per asset, no directories.
// That is not cosmetic. Refusing any entry whose name is not a bare asset name
// closes zip-slip outright — there is no path to traverse because a name is
// never a path. Entry count and total uncompressed size are capped, so a zip
// bomb is refused rather than expanded.

// Serialise. `theme.json` is written FIRST so a human opening the archive sees
// it at the top. Returns "" on failure (the caller reports a 500 — we produced
// this bundle, so a failure here is ours, not the studio's).
std::string ThemeBundleToZip(const ThemeBundle& bundle);

// Parse. `zipBytes` is untrusted: it arrived over HTTP from a file picker.
//
// Fills `jsonOut` (the raw parsed theme.json, BEFORE migration — the importer
// owns that) and `assetsOut`. Returns "" on success, or a reason suitable for a
// 400. Never throws for bad input.
std::string ThemeBundleFromZip(
    std::string_view zipBytes,
    Json::Value& jsonOut,
    std::map<std::string, std::string>& assetsOut);

}  // namespace Branding
