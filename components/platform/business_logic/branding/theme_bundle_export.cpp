#include "business_logic/branding/theme_bundle_export.h"

#include <set>
#include <vector>

#include "business_logic/branding/site_content_slots.h"
#include "business_logic/branding/site_font_inventory.h"
#include "business_logic/branding/site_theme_tokens.h"
#include "business_logic/branding/theme_bundle_assets.h"
#include "business_logic/branding/theme_bundle_migrations.h"
#include "business_logic/branding/theme_bundle_sections.h"
#include "db_schema/site_fonts.h"
#include "sql_util/table_helpers/site_assets.h"
#include "sql_util/table_helpers/site_fonts.h"
#include "util/secrets/secret_keys.h"
#include "util/types.h"

namespace Branding {
namespace {

std::string ValueOr(const KeyValueTable& row, std::string_view column) {
    auto it = row.find(std::string(column));
    return it == row.end() ? std::string() : it->second;
}

// Give an asset a name nothing else has taken. Deterministic: the suffix is
// derived from how many times this stem has already been used, so the same
// inventory always produces the same names.
std::string ClaimAssetName(
    const ThemeBundle& bundle,
    std::string_view stem,
    std::string_view extension) {
    std::string name = MakeAssetName(stem, extension);
    if (name.empty()) {
        return {};
    }
    if (!bundle.HasAsset(name)) {
        return name;
    }
    for (int suffix = 2; suffix < 1000; ++suffix) {
        std::string candidate =
            MakeAssetName(std::string(stem) + "-" + StringFromInt(suffix), extension);
        if (!candidate.empty() && !bundle.HasAsset(candidate)) {
            return candidate;
        }
    }
    return {};
}

}  // namespace

std::string ExportThemeBundle(
    DatabaseHelper databaseHelper,
    Transaction& transaction,
    Secrets::SecretsHelper& secrets,
    const ThemeBundleExportOptions& options,
    ThemeBundle& out) {

    out = ThemeBundle{};
    out.formatVersion = CurrentBundleFormatVersion();
    out.name = options.name;
    out.description = options.description;
    out.exportedAt = options.exportedAt;
    out.exportedFromApp = options.app;
    out.exportedFromSite = options.site;
    out.exportedFromHonuware = options.honuwareVersion;

    // ---- content slots (allow-listed) ----
    //
    // A URL slot pointing at one of this tenant's own site assets comes back
    // out as the bare filename it went in as, and the bytes travel with it —
    // that is what makes a theme portable rather than a set of links into the
    // tenant it came from. An EXTERNAL url is left exactly as it is.
    TableHelpers::SiteAssets siteAssets(databaseHelper);
    auto exportSlot = [&](std::string_view key) -> std::string {
        std::string value = secrets.LookupSecret(transaction, key);
        const std::string prefix(kThemeBundleAssetUrlPrefix);
        if (value.rfind(prefix, 0) != 0) {
            return value;
        }
        const std::string name = value.substr(prefix.size());
        if (!IsValidBundleAssetName(name)) {
            return value;
        }
        const std::string bytes = siteAssets.GetAssetBytes(transaction, name);
        if (bytes.empty()) {
            // The row is gone but the slot still points at it. Exporting the
            // dangling reference would produce a bundle that fails its own
            // validation, so the slot is cleared instead.
            return {};
        }
        out.assets[name] = bytes;
        return name;
    };

    out.content[std::string(Secrets::kSiteLogoUrl)] =
        exportSlot(Secrets::kSiteLogoUrl);
    for (const ContentSlot& slot : SiteContentSlots()) {
        out.content[std::string(slot.key)] =
            slot.type == SlotType::Url ? exportSlot(slot.key)
                                       : secrets.LookupSecret(transaction, slot.key);
    }

    // ---- theme tokens ----
    for (const ThemeToken& token : SiteThemeTokens()) {
        out.tokens[std::string(token.key)] =
            secrets.LookupSecret(transaction, token.key);
    }

    // ---- fonts ----
    TableHelpers::SiteFonts fonts(databaseHelper);

    // source_font_id -> source_key, so a family can name its source rather than
    // carrying an id a bundle must never contain.
    std::map<std::string, std::string> sourceKeyById;
    for (const KeyValueTable& row : fonts.GetAllSources(transaction)) {
        BundleFontSource source;
        source.sourceKey = ValueOr(row, DbSchema::kSiteFontSourceKey);
        source.displayName = ValueOr(row, DbSchema::kSiteFontSourceDisplayName);
        source.baseUrl = ValueOr(row, DbSchema::kSiteFontSourceBaseUrl);
        source.querySuffix = ValueOr(row, DbSchema::kSiteFontSourceQuerySuffix);
        for (const FontPreconnect& preconnect : ParsePreconnectLines(
                 ValueOr(row, DbSchema::kSiteFontSourcePreconnectLines))) {
            source.preconnects.push_back(
                BundleFontPreconnect{preconnect.href, preconnect.crossorigin});
        }
        sourceKeyById[ValueOr(row, DbSchema::kSiteFontSourceId)] = source.sourceKey;
        out.fonts.sources.push_back(source);
    }

    // GetAllFonts is ordered by ordinal, and array order IS the ordinal in the
    // file — so the ordering survives without carrying the column.
    for (const KeyValueTable& row : fonts.GetAllFonts(transaction)) {
        BundleFontFamily family;
        family.family = ValueOr(row, DbSchema::kSiteFontFamily);
        family.fallback = ValueOr(row, DbSchema::kSiteFontFallback);
        family.sourceKind = ValueOr(row, DbSchema::kSiteFontSourceKind);
        if (family.sourceKind == DbSchema::kSiteFontSourceKindCdn) {
            auto it = sourceKeyById.find(
                ValueOr(row, DbSchema::kSiteFontFontSourceId));
            family.sourceKey = it == sourceKeyById.end() ? std::string() : it->second;
            family.spec = ValueOr(row, DbSchema::kSiteFontSpec);
        }

        const int64_t fontId =
            std::atoll(ValueOr(row, DbSchema::kSiteFontId).c_str());
        for (const KeyValueTable& faceRow :
             fonts.GetFacesForFont(transaction, fontId)) {
            const int64_t faceId =
                std::atoll(ValueOr(faceRow, DbSchema::kSiteFontFaceId).c_str());
            const std::string format = ValueOr(faceRow, DbSchema::kSiteFontFaceFormat);
            const std::string extension = ExtensionForFontFormat(format);
            if (extension.empty()) {
                return "Font \"" + family.family +
                       "\" has a file in an unknown format (" + format + ").";
            }
            BundleFontFace face;
            face.weight = std::atoi(
                ValueOr(faceRow, DbSchema::kSiteFontFaceWeight).c_str());
            face.style = ValueOr(faceRow, DbSchema::kSiteFontFaceStyle);
            // Family + weight + style is what makes this stable AND readable:
            // "studiosans-700-italic.woff2".
            const std::string stem = family.family + "-" +
                                     StringFromInt(face.weight) + "-" + face.style;
            face.file = ClaimAssetName(out, stem, extension);
            if (face.file.empty()) {
                return "Could not name the font file for \"" + family.family + "\".";
            }
            const std::string bytes = fonts.GetFaceBytes(transaction, faceId);
            if (bytes.empty()) {
                // A face row with no bytes would export a bundle whose import
                // fails. Better to say so here, where the tenant is known.
                return "The font file for \"" + family.family +
                       "\" could not be read.";
            }
            out.assets[face.file] = bytes;
            family.faces.push_back(face);
        }
        out.fonts.families.push_back(family);
    }

    // ---- app sections ----
    SectionContext context;
    context.databaseHelper = &databaseHelper;
    context.transaction = &transaction;
    context.bundle = &out;
    for (const ThemeBundleSection& section : ThemeBundleSections()) {
        Json::Value body;
        const std::string reason = section.exporter(context, body);
        if (!reason.empty()) {
            return reason;
        }
        out.appSections[section.name] = body;
    }

    // ---- whole-bundle asset checks ----
    std::vector<std::string> names;
    std::size_t totalBytes = 0;
    for (const auto& [name, bytes] : out.assets) {
        if (!IsValidBundleAssetName(name)) {
            return "\"" + name + "\" is not a usable file name.";
        }
        if (bytes.size() > kMaxBundleAssetBytes) {
            return "\"" + name + "\" is too large to put in a theme file.";
        }
        names.push_back(name);
        totalBytes += bytes.size();
    }
    if (out.assets.size() > kMaxBundleAssets) {
        return "This theme has more files than a theme file can hold.";
    }
    if (totalBytes > kMaxBundleTotalBytes) {
        return "This theme's files add up to more than a theme file can hold.";
    }
    // Two names differing only in case would collapse into one file on macOS or
    // Windows, silently losing an asset.
    const std::string duplicate = FindCaseInsensitiveDuplicate(names);
    if (!duplicate.empty()) {
        return "Two files in this theme are both called \"" + duplicate + "\".";
    }

    return {};
}

}  // namespace Branding
