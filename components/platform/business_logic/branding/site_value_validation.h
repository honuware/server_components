#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Branding {

// Tenant Theming and Branding, decision D10 — "validation is server-side and
// strict, and a junk value falls back to the default rather than breaking every
// page of a tenant's site."
//
// Two entry points, one rule set:
//   * ValidateSlotValue  — the WRITE path (Phase 6's PUT /api/manage/site_theme):
//                          returns a human-readable reason so the admin sees why
//                          a value was refused.
//   * NormalizeSlotValue — the READ path (GET /api/site_info): repairs what is
//                          repairable (CRLF line endings) and blanks what is
//                          not, so a hand-edited database row degrades to the
//                          SPA's bundled default instead of a broken page.
//
// An EMPTY value is valid for every type: empty means "unset — use the bundled
// default", the contract kSiteLogoUrl established.

// How a slot's value is shaped. Drives both validation and the read-path
// normalization; the per-slot assignment lives in site_content_slots.h.
enum class SlotType {
    Line,      // single-line string (headline, email address, button label)
    Lines,     // ordered list, stored newline-separated (address, tagline)
    Markdown,  // long prose rendered through the SPA's markdown pipeline
    Url,       // absolute http(s) or root-relative link
    Color,     // #RRGGBB design token (used by the Phase 4 theme object)
};

// Byte caps. Generous enough that no legitimate value trips them, small enough
// that a tenant cannot push a megabyte of text through a cached public endpoint.
inline constexpr std::size_t kMaxLineBytes = 1024;
inline constexpr std::size_t kMaxLinesBytes = 4096;
inline constexpr std::size_t kMaxMarkdownBytes = 65536;  // 64 KB, per D10
inline constexpr std::size_t kMaxUrlBytes = 2048;

// `#RRGGBB` only — exactly seven characters, six hex digits. Shorthand (#RGB),
// alpha (#RRGGBBAA), and named/functional colors are deliberately refused: the
// token layer writes these straight into a CSS custom property, so one
// canonical form keeps the pickers, the tests, and the stylesheet in agreement.
bool IsValidHexColor(std::string_view value);

// Absolute `http://` / `https://`, or root-relative `/path`. Protocol-relative
// `//host/path` is refused — it silently points at a third-party origin, which
// is exactly what a tenant-editable URL must not be able to do by accident.
// No whitespace or control characters anywhere in the value.
bool IsValidSiteUrl(std::string_view value);

// A CSS length for a radius/size token: a number with a unit (`8px`, `0.5rem`,
// `50%`), or a bare `0`. Deliberately NOT a general CSS value — these land in a
// custom property, and `calc()`, `var()` and function syntax would let a tenant
// smuggle arbitrary CSS through a field labelled "corner radius".
bool IsValidCssLength(std::string_view value);

// A CSS font-family list: `Roboto, Arial, sans-serif` or `"Din Bold", serif`.
// Letters, digits, spaces, hyphens, underscores, commas and double quotes only.
// The characters that would end the declaration or open a function — `;` `{` `}`
// `(` `)` `\` `/` — are refused, so a family name cannot become a rule of its own.
bool IsValidFontFamilyList(std::string_view value);

// Returns an empty string when `value` is acceptable for `type`, otherwise a
// short reason suitable for an HTTP 400 body. Assumes `value` has already been
// through NormalizeLineEndings when the caller accepts CRLF input.
std::string ValidateSlotValue(SlotType type, std::string_view value);

// CRLF (and lone CR) -> LF. Windows admin pasting into a textarea is the normal
// case, not an error, so the read and write paths both repair it rather than
// rejecting it.
std::string NormalizeLineEndings(std::string_view value);

// Read-path repair-or-blank: normalizes line endings, then returns the value if
// it validates and an empty string if it does not.
std::string NormalizeSlotValue(SlotType type, std::string_view value);

}  // namespace Branding
