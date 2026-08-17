#pragma once

#include <map>
#include <string>

#include "business_logic/branding/theme_bundle.h"

namespace Branding {

// Tenant Theming Phase 9 — the bundle as a DIRECTORY on disk.
//
// The second of the two transports (the other is theme_bundle_zip). This is the
// form Mason asked for — "images … loaded from and saved to the same directory
// as the json" — and the one that makes a theme reviewable as a text diff with
// its images beside it, which is what a `.zip` in git cannot be.
//
// Same format, same asset-name rule. A file in the directory whose name is not
// a bare asset name is refused rather than resolved, exactly as a zip entry is.

// Writes `theme.json` plus one file per asset into `directory`.
//
// Refuses to write into a directory that already holds a theme unless `force` —
// overwriting someone's theme because they mistyped a path is not recoverable.
// Returns "" on success, or a reason.
std::string WriteThemeBundleDirectory(
    const ThemeBundle& bundle, const std::string& directory, bool force);

// Reads `theme.json` and every sibling file. Fills `jsonOut` (raw, BEFORE
// migration — the importer owns that) and `assetsOut`.
//
// Subdirectories are ignored rather than descended into: a bundle is flat, and
// silently flattening a tree would let two files collide.
std::string ReadThemeBundleDirectory(
    const std::string& directory,
    Json::Value& jsonOut,
    std::map<std::string, std::string>& assetsOut);

}  // namespace Branding
