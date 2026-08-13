#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class Transaction;
class DatabaseHelper;

namespace Branding {

// Tenant Theming Phase 4B — turning the tenant's font rows into what a page
// actually needs: preconnect hints, one stylesheet URL per source, and an
// @font-face descriptor per uploaded face.
//
// See db_schema/site_fonts.h for the data model and the plan's D12/D13/D14 for
// the decisions behind it.

// ---- validation -------------------------------------------------------------

// Per-face upload cap. A woff2 of a full weight range is comfortably under a
// megabyte; 5 MB leaves room for an uncompressed OTF without letting a tenant
// push arbitrary payloads into a public, cacheable endpoint.
inline constexpr std::size_t kMaxFontFaceBytes = 5u * 1024u * 1024u;

// The format a face's MAGIC BYTES say it is — "woff2", "woff", "ttf", "otf", or
// "" when the bytes are not a font at all. Never trust the filename: this is
// what stops an uploaded .woff2 that is really a script from being stored and
// then served back from our own origin.
std::string FontFormatFromMagicBytes(std::string_view bytes);

// A font source's base URL or preconnect href. https only, well-formed, no
// whitespace or control characters, length-capped.
//
// D12 dropped the origin allow-list at Mason's direction, so this is hygiene,
// not policy — it stops a malformed row breaking every page, and it refuses
// `http://` so a tenant cannot downgrade its own visitors.
bool IsValidFontSourceUrl(std::string_view value);

// A CSS font family NAME (not a stack) — what a site_fonts row stores and what
// a role token names. Letters, digits, spaces, hyphens, underscores only, so it
// can be quoted into a declaration safely.
bool IsValidFontFamilyName(std::string_view value);

// The stylesheet query fragment for a `cdn` row, e.g.
// "family=Barlow:wght@100..900". Refuses anything that could escape the query
// string or add a second parameter of its own choosing.
bool IsValidFontSpec(std::string_view value);

// ---- the inventory ----------------------------------------------------------

struct FontPreconnect {
    std::string href;
    bool crossorigin = false;
};

struct FontFaceDescriptor {
    std::string family;
    int weight = 400;
    std::string style;   // "normal" / "italic"
    std::string format;  // woff2 / woff / ttf / otf
    std::string url;     // the serving endpoint for this face
};

struct SiteFontInventory {
    std::vector<FontPreconnect> preconnects;
    // One per source that has at least one active `cdn` family — NOT one per
    // font. Mason's own example is the shape: several `family=` fragments joined
    // into a single request.
    std::vector<std::string> stylesheets;
    std::vector<FontFaceDescriptor> faces;
};

// Parses a source's `preconnect_lines` column (`href|crossorigin` per line, the
// site_social_links precedent). Invalid hrefs are skipped rather than emitted.
std::vector<FontPreconnect> ParsePreconnectLines(std::string_view lines);

// baseUrl + "?" + specs joined with "&" + the source's query suffix.
// Returns "" when there is nothing to ask for or the base URL is unusable.
std::string BuildFontStylesheetUrl(
    std::string_view baseUrl,
    const std::vector<std::string>& specs,
    std::string_view querySuffix);

// `'Barlow', sans-serif` — D13's composition. The family is quoted so a
// multi-word name works; the fallback is emitted verbatim because it is a
// generic family or a stack, and it is NEVER defaulted when absent.
std::string ComposeFontStack(std::string_view family, std::string_view fallback);

// Reads the tenant's whole font inventory and builds the payload above.
// `faceUrlPrefix` is the serving route each face id is appended to.
SiteFontInventory LoadSiteFontInventory(
    DatabaseHelper databaseHelper,
    Transaction& transaction,
    std::string_view faceUrlPrefix);

// The composed stack for one family name, or "" when the tenant has no row for
// it. Used to resolve the `--font-*` role tokens (D13).
std::string LookupFontStack(
    DatabaseHelper databaseHelper,
    Transaction& transaction,
    std::string_view family);

}  // namespace Branding
