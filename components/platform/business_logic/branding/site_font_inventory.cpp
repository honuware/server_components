#include "business_logic/branding/site_font_inventory.h"

#include <cstdlib>
#include <map>

#include "db_schema/site_fonts.h"
#include "sql_util/table_helpers/site_fonts.h"
#include "util/types.h"

namespace Branding {
namespace {

constexpr std::size_t kMaxUrlBytes = 2048;
constexpr std::size_t kMaxFamilyBytes = 64;
constexpr std::size_t kMaxSpecBytes = 256;

bool HasControlOrSpace(std::string_view value) {
    for (char c : value) {
        unsigned char byte = static_cast<unsigned char>(c);
        if (byte <= 0x20 || byte == 0x7F) {
            return true;
        }
    }
    return false;
}

std::string ValueOr(
    const KeyValueTable& row, std::string_view column, std::string fallback = "") {
    auto it = row.find(std::string(column));
    return it == row.end() ? fallback : it->second;
}

bool IsTrue(std::string_view value) {
    return value == "t" || value == "true" || value == "TRUE";
}

}  // namespace

// ---- validation -------------------------------------------------------------

std::string FontFormatFromMagicBytes(std::string_view bytes) {
    if (bytes.size() < 4) {
        return "";
    }
    const unsigned char* data =
        reinterpret_cast<const unsigned char*>(bytes.data());
    // wOF2 / wOFF — the two web font wrappers.
    if (bytes.compare(0, 4, "wOF2") == 0) return "woff2";
    if (bytes.compare(0, 4, "wOFF") == 0) return "woff";
    // OpenType with CFF outlines.
    if (bytes.compare(0, 4, "OTTO") == 0) return "otf";
    // TrueType: version 1.0 (00 01 00 00) or the legacy 'true' tag.
    if (data[0] == 0x00 && data[1] == 0x01 && data[2] == 0x00 && data[3] == 0x00) {
        return "ttf";
    }
    if (bytes.compare(0, 4, "true") == 0) return "ttf";
    if (bytes.compare(0, 4, "ttcf") == 0) return "ttf";
    return "";
}

bool IsValidFontSourceUrl(std::string_view value) {
    if (value.empty() || value.size() > kMaxUrlBytes) {
        return false;
    }
    if (HasControlOrSpace(value)) {
        return false;
    }
    // https only — a tenant must not be able to downgrade its own visitors.
    if (value.rfind("https://", 0) != 0 || value.size() <= 8) {
        return false;
    }
    // A quote or angle bracket would break out of the attribute this lands in.
    for (char c : value) {
        if (c == '"' || c == '\'' || c == '<' || c == '>' || c == '\\') {
            return false;
        }
    }
    return true;
}

bool IsValidFontFamilyName(std::string_view value) {
    if (value.empty() || value.size() > kMaxFamilyBytes) {
        return false;
    }
    for (char c : value) {
        bool allowed =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

bool IsValidFontSpec(std::string_view value) {
    if (value.empty() || value.size() > kMaxSpecBytes) {
        return false;
    }
    if (HasControlOrSpace(value)) {
        return false;
    }
    // The Google Fonts v2 grammar, which is richer than it first looks:
    //   family=Barlow:wght@100..900
    //   family=Roboto:ital,wght@0,300;0,400;1,400
    // The SEMICOLON is load-bearing — it separates axis tuples, and the app's
    // own shipped font URL uses it. '+' encodes a space in a family name, '%'
    // allows ordinary percent-encoding.
    //
    // Deliberately absent: '&', '?' and '#'. A row contributes ONE parameter,
    // and letting it smuggle a second is how a spec turns into a different
    // request than the admin configured.
    for (char c : value) {
        bool allowed =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '=' || c == ':' || c == '@' || c == '.' || c == ',' ||
            c == '+' || c == '-' || c == '_' || c == ';' || c == '%';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

// ---- the inventory ----------------------------------------------------------

std::vector<FontPreconnect> ParsePreconnectLines(std::string_view lines) {
    std::vector<FontPreconnect> out;
    std::size_t start = 0;
    while (start <= lines.size()) {
        std::size_t end = lines.find('\n', start);
        std::string_view line = lines.substr(
            start, end == std::string_view::npos ? std::string_view::npos
                                                 : end - start);
        start = end == std::string_view::npos ? lines.size() + 1 : end + 1;

        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.remove_suffix(1);
        }
        while (!line.empty() && line.front() == ' ') {
            line.remove_prefix(1);
        }
        if (line.empty()) {
            continue;
        }
        std::size_t bar = line.find('|');
        std::string_view href = bar == std::string_view::npos
            ? line : line.substr(0, bar);
        std::string_view flag = bar == std::string_view::npos
            ? std::string_view() : line.substr(bar + 1);
        if (!IsValidFontSourceUrl(href)) {
            continue;
        }
        out.push_back({std::string(href), IsTrue(flag)});
    }
    return out;
}

std::string BuildFontStylesheetUrl(
    std::string_view baseUrl,
    const std::vector<std::string>& specs,
    std::string_view querySuffix) {
    if (specs.empty() || !IsValidFontSourceUrl(baseUrl)) {
        return "";
    }
    std::string url(baseUrl);
    url += "?";
    bool first = true;
    for (const std::string& spec : specs) {
        if (!IsValidFontSpec(spec)) {
            continue;
        }
        if (!first) {
            url += "&";
        }
        url += spec;
        first = false;
    }
    if (first) {
        // Every spec was junk — asking for nothing is worse than not asking.
        return "";
    }
    if (!querySuffix.empty() && IsValidFontSpec(querySuffix)) {
        url += "&";
        url += std::string(querySuffix);
    }
    return url;
}

std::string ComposeFontStack(std::string_view family, std::string_view fallback) {
    if (!IsValidFontFamilyName(family)) {
        return "";
    }
    std::string stack = "'" + std::string(family) + "'";
    // D13: the fallback is data, never assumed. A row without one yields the
    // family alone rather than a guessed generic.
    if (!fallback.empty()) {
        stack += ", ";
        stack += std::string(fallback);
    }
    return stack;
}

SiteFontInventory LoadSiteFontInventory(
    DatabaseHelper databaseHelper,
    Transaction& transaction,
    std::string_view faceUrlPrefix) {
    SiteFontInventory inventory;
    TableHelpers::SiteFonts fonts(databaseHelper);

    // Sources, by id, so a font row can find its own.
    std::map<std::string, KeyValueTable> sourcesById;
    for (const KeyValueTable& row : fonts.GetActiveSources(transaction)) {
        sourcesById[ValueOr(row, DbSchema::kSiteFontSourceId)] = row;
    }

    // Each source's `cdn` specs, in font order.
    std::map<std::string, std::vector<std::string>> specsBySource;
    for (const KeyValueTable& font : fonts.GetActiveFonts(transaction)) {
        if (ValueOr(font, DbSchema::kSiteFontSourceKind) !=
            DbSchema::kSiteFontSourceKindCdn) {
            continue;
        }
        std::string sourceId = ValueOr(font, DbSchema::kSiteFontFontSourceId);
        std::string spec = ValueOr(font, DbSchema::kSiteFontSpec);
        if (sourceId.empty() || spec.empty() ||
            sourcesById.find(sourceId) == sourcesById.end()) {
            continue;
        }
        specsBySource[sourceId].push_back(spec);
    }

    // One stylesheet per source, and that source's preconnects — emitted only
    // for sources actually in use, so a configured-but-unused source costs a
    // visitor nothing.
    for (const auto& [sourceId, specs] : specsBySource) {
        const KeyValueTable& source = sourcesById[sourceId];
        std::string url = BuildFontStylesheetUrl(
            ValueOr(source, DbSchema::kSiteFontSourceBaseUrl),
            specs,
            ValueOr(source, DbSchema::kSiteFontSourceQuerySuffix));
        if (url.empty()) {
            continue;
        }
        inventory.stylesheets.push_back(url);
        for (FontPreconnect preconnect : ParsePreconnectLines(
                 ValueOr(source, DbSchema::kSiteFontSourcePreconnectLines))) {
            bool seen = false;
            for (const FontPreconnect& existing : inventory.preconnects) {
                if (existing.href == preconnect.href) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                inventory.preconnects.push_back(preconnect);
            }
        }
    }

    // Uploaded faces.
    for (const KeyValueTable& face : fonts.GetAllActiveFaces(transaction)) {
        FontFaceDescriptor descriptor;
        descriptor.family = ValueOr(face, DbSchema::kSiteFontFamily);
        if (!IsValidFontFamilyName(descriptor.family)) {
            continue;
        }
        std::string weight = ValueOr(face, DbSchema::kSiteFontFaceWeight, "400");
        descriptor.weight = weight.empty() ? 400 : std::atoi(weight.c_str());
        descriptor.style = ValueOr(face, DbSchema::kSiteFontFaceStyle, "normal");
        descriptor.format = ValueOr(face, DbSchema::kSiteFontFaceFormat);
        descriptor.url = std::string(faceUrlPrefix) +
            ValueOr(face, DbSchema::kSiteFontFaceId);
        if (descriptor.format.empty() || descriptor.url.empty()) {
            continue;
        }
        inventory.faces.push_back(descriptor);
    }

    return inventory;
}

std::string LookupFontStack(
    DatabaseHelper databaseHelper,
    Transaction& transaction,
    std::string_view family) {
    if (!IsValidFontFamilyName(family)) {
        return "";
    }
    TableHelpers::SiteFonts fonts(databaseHelper);
    KeyValueTable row = fonts.GetFontByFamily(transaction, family);
    if (row.empty()) {
        return "";
    }
    return ComposeFontStack(
        ValueOr(row, DbSchema::kSiteFontFamily),
        ValueOr(row, DbSchema::kSiteFontFallback));
}

}  // namespace Branding
