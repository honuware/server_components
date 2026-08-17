#pragma once

#include <string>
#include <vector>

#include "business_logic/branding/theme_bundle.h"
#include "util/json_value.h"

namespace Branding {

// Tenant Theming Phase 9 — ThemeBundle <-> theme.json.
//
// This file owns the ONE place the file shape deliberately differs from
// storage. Three slot types are stored newline-packed because
// `config_secrets.value` is a single text column; in the file they take their
// natural JSON shape, because a theme file is meant to be edited by a person:
//
//   lines             "a\nb"                  <-> ["a", "b"]
//   site_social_links "Instagram|https://…"   <-> [{"label":…, "url":…}]
//   preconnect_lines  "https://x|true"        <-> [{"url":…, "crossorigin":…}]
//   ordinal           a column                <-> array order
//
// Everything else is a straight copy. Key emission is driven by the registries
// (SiteContentSlots / SiteThemeTokens), never a hand-written list — a guard
// test asserts that, so a new token cannot silently fall out of the format.

// ---- the packing conversions, exposed for their own tests ------------------

// "a\nb" <-> ["a","b"]. An empty stored value is an EMPTY list, not a list
// containing one empty string — otherwise a round-trip would grow a blank line.
std::vector<std::string> UnpackLines(std::string_view stored);
std::string PackLines(const std::vector<std::string>& lines);

struct BundleLabelledLink {
    std::string label;
    std::string url;
};

// "Label|https://…" per line. A line with no `|` keeps the whole line as the
// label and an empty url, which is what the site_info reader already does —
// the file must not be stricter than the store, or an existing tenant would
// fail to export.
std::vector<BundleLabelledLink> UnpackLabelledLinks(std::string_view stored);
std::string PackLabelledLinks(const std::vector<BundleLabelledLink>& links);

// ---- whole-bundle conversion -----------------------------------------------

// Serialise. Emits a key for EVERY registered content slot and theme token,
// including unset ones (as ""), so a bundle is a complete statement of a look
// rather than a diff against whatever the exporting tenant happened to have.
Json::Value ThemeBundleToJson(const ThemeBundle& bundle);

// Parse. Structural only: it fills the struct and reports shapes it cannot
// read. It does NOT check that keys are known, that assets resolve, or that
// values validate — migration runs between parse and validation, so those
// checks belong to theme_bundle_import.
//
// Returns "" on success, or a human-readable reason.
std::string ThemeBundleFromJson(const Json::Value& json, ThemeBundle& out);

// The `format_version` of a parsed document, without parsing the rest — the
// migration chain needs this before it can decide what to run. Returns 0 when
// the field is missing or unreadable.
int ReadBundleFormatVersion(const Json::Value& json);

}  // namespace Branding
