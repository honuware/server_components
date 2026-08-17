#include "business_logic/branding/theme_bundle_import.h"

#include <set>
#include <vector>

#include "business_logic/branding/site_content_slots.h"
#include "business_logic/branding/site_font_inventory.h"
#include "business_logic/branding/site_theme_tokens.h"
#include "business_logic/branding/theme_bundle_assets.h"
#include "business_logic/branding/theme_bundle_json.h"
#include "business_logic/branding/theme_bundle_sections.h"
#include "db_schema/site_assets.h"
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

const ContentSlot* FindContentSlot(const std::string& key) {
    for (const ContentSlot& slot : SiteContentSlots()) {
        if (key == slot.key) {
            return &slot;
        }
    }
    return nullptr;
}

const ThemeToken* FindThemeToken(const std::string& key) {
    for (const ThemeToken& token : SiteThemeTokens()) {
        if (key == token.key) {
            return &token;
        }
    }
    return nullptr;
}

// A URL slot may hold either a bundled filename or an external URL. The site
// stores a servable URL, so a bundled file is rewritten to the route that will
// serve it once the asset is placed.
bool IsUrlSlot(const std::string& key) {
    if (key == Secrets::kSiteLogoUrl) {
        return true;
    }
    const ContentSlot* slot = FindContentSlot(key);
    return slot != nullptr && slot->type == SlotType::Url;
}

}  // namespace

std::string ValidateThemeBundle(
    const ThemeBundle& bundle,
    BundleStrictness strictness,
    std::vector<std::string>& unknownKeysOut) {

    // ---- assets ----
    if (bundle.assets.size() > kMaxBundleAssets) {
        return "That theme file has more files in it than we can accept.";
    }
    std::vector<std::string> names;
    std::size_t totalBytes = 0;
    for (const auto& [name, bytes] : bundle.assets) {
        if (!IsValidBundleAssetName(name)) {
            // The security boundary. A name that is a path never gets as far as
            // being resolved — it is refused for being the wrong shape.
            return "\"" + name + "\" is not a usable file name.";
        }
        if (bytes.size() > kMaxBundleAssetBytes) {
            return "\"" + name + "\" is larger than we can accept.";
        }
        // Every asset is stored and then served back from OUR origin, so what
        // each one IS gets decided by its content — never by its name. A file
        // that is neither a font nor an image has no business in a theme, and
        // this is the check that stops a zip being used to host one.
        if (FontFormatFromMagicBytes(bytes).empty() &&
            ImageTypeFromMagicBytes(bytes).empty()) {
            return "\"" + name + "\" is not an image or a font.";
        }
        names.push_back(name);
        totalBytes += bytes.size();
    }
    if (totalBytes > kMaxBundleTotalBytes) {
        return "That theme file's contents add up to more than we can accept.";
    }
    const std::string duplicate = FindCaseInsensitiveDuplicate(names);
    if (!duplicate.empty()) {
        return "Two files in that theme are both called \"" + duplicate + "\".";
    }

    // ---- content ----
    for (const auto& [key, value] : bundle.content) {
        const bool known = key == Secrets::kSiteLogoUrl || FindContentSlot(key);
        if (!known) {
            // Refused under BOTH modes when it is not a site_ key: config_secrets
            // holds live credentials, and "lenient" must never become the way a
            // mail password arrives in something labelled a theme.
            if (key.rfind("site_", 0) != 0) {
                return "\"" + key + "\" is not a theme setting.";
            }
            unknownKeysOut.push_back(key);
            continue;
        }
        if (value.empty()) {
            continue;  // empty means "use the default"
        }
        if (IsUrlSlot(key) && IsBundleAssetReference(value)) {
            if (!bundle.HasAsset(value)) {
                return "\"" + key + "\" points at \"" + value +
                       "\", which is not in that theme file.";
            }
            continue;  // a bundled file, not a URL to validate
        }
        const ContentSlot* slot = FindContentSlot(key);
        if (slot) {
            const std::string reason =
                ValidateSlotValue(slot->type, NormalizeLineEndings(value));
            if (!reason.empty()) {
                return key + ": " + reason;
            }
        }
    }

    // ---- tokens ----
    for (const auto& [key, value] : bundle.tokens) {
        const ThemeToken* token = FindThemeToken(key);
        if (!token) {
            if (key.rfind("site_", 0) != 0) {
                return "\"" + key + "\" is not a theme setting.";
            }
            unknownKeysOut.push_back(key);
            continue;
        }
        if (value.empty()) {
            continue;  // clearing a token IS "reset to default"
        }
        if (!IsValidThemeTokenValue(token->type, value)) {
            return key + ": \"" + value + "\" is not a valid value for that setting.";
        }
    }

    if (strictness == BundleStrictness::Strict && !unknownKeysOut.empty()) {
        return "That theme file has settings this site does not have (" +
               unknownKeysOut.front() +
               (unknownKeysOut.size() > 1
                    ? " and " + StringFromInt(
                          static_cast<int64_t>(unknownKeysOut.size() - 1)) + " more"
                    : "") +
               ").";
    }

    // ---- fonts ----
    std::set<std::string> sourceKeys;
    for (const BundleFontSource& source : bundle.fonts.sources) {
        if (source.sourceKey.empty()) {
            return "Every font service in that theme needs a short key.";
        }
        if (!sourceKeys.insert(source.sourceKey).second) {
            return "Two font services in that theme share the key \"" +
                   source.sourceKey + "\".";
        }
        if (!IsValidFontSourceUrl(source.baseUrl)) {
            return source.sourceKey +
                   ": the font service address must be an https:// URL.";
        }
        if (!source.querySuffix.empty() && !IsValidFontSpec(source.querySuffix)) {
            return source.sourceKey + ": that extra setting is not valid.";
        }
        for (const BundleFontPreconnect& preconnect : source.preconnects) {
            if (!IsValidFontSourceUrl(preconnect.url)) {
                return source.sourceKey + ": \"" + preconnect.url +
                       "\" is not a usable address to warm up.";
            }
        }
    }

    std::set<std::string> familyNames;
    for (const BundleFontFamily& family : bundle.fonts.families) {
        if (!IsValidFontFamilyName(family.family)) {
            return "\"" + family.family + "\" is not a usable font name.";
        }
        if (!familyNames.insert(family.family).second) {
            return "Two fonts in that theme are both called \"" +
                   family.family + "\".";
        }
        // D13: the fallback is required and never assumed.
        if (family.fallback.empty() || !IsValidFontFamilyList(family.fallback)) {
            return family.family + ": needs a backup font (for example sans-serif).";
        }
        if (family.sourceKind == DbSchema::kSiteFontSourceKindCdn) {
            if (!sourceKeys.count(family.sourceKey)) {
                return family.family + ": names a font service (\"" +
                       family.sourceKey + "\") that theme does not include.";
            }
            if (!IsValidFontSpec(family.spec)) {
                return family.family + ": that font specification is not valid.";
            }
        } else if (family.sourceKind == DbSchema::kSiteFontSourceKindUploaded) {
            if (family.faces.empty()) {
                return family.family +
                       ": has no font files, so nothing could be shown in it.";
            }
            for (const BundleFontFace& face : family.faces) {
                if (face.weight < 1 || face.weight > 1000) {
                    return family.family + ": " + StringFromInt(face.weight) +
                           " is not a usable font weight.";
                }
                auto asset = bundle.assets.find(face.file);
                if (asset == bundle.assets.end()) {
                    return family.family + ": the file \"" + face.file +
                           "\" is not in that theme file.";
                }
                // D14: the format comes from the MAGIC BYTES, never the name.
                // This is what stops a renamed .woff2 that is really HTML being
                // stored and then served back from our own origin.
                if (FontFormatFromMagicBytes(asset->second).empty()) {
                    return family.family + ": \"" + face.file +
                           "\" is not a WOFF2, WOFF, TTF or OTF font.";
                }
            }
        } else if (family.sourceKind != DbSchema::kSiteFontSourceKindSystem) {
            return family.family + ": unknown font kind \"" +
                   family.sourceKind + "\".";
        }
    }

    return {};
}

BundleImportReport ImportThemeBundleJson(
    DatabaseHelper databaseHelper,
    Transaction& transaction,
    Secrets::SecretsHelper& secrets,
    const Json::Value& json,
    const std::map<std::string, std::string>& assets,
    const ThemeBundleImportOptions& options) {

    BundleImportReport report;

    // 1. Migrate FIRST, so a bundle that is merely old is repaired rather than
    //    reported as a pile of unknown keys.
    Json::Value migrated = json;
    const int originalVersion = ReadBundleFormatVersion(migrated);
    const std::string migrationError =
        MigrateBundleJson(migrated, report.migrationsApplied);
    if (!migrationError.empty()) {
        report.error = migrationError;
        return report;
    }
    if (!report.migrationsApplied.empty()) {
        report.migratedFrom = originalVersion;
    }

    // 2. Parse.
    ThemeBundle bundle;
    const std::string parseError = ThemeBundleFromJson(migrated, bundle);
    if (!parseError.empty()) {
        report.error = parseError;
        return report;
    }
    bundle.assets = assets;

    // 3. Validate the WHOLE thing before writing any of it.
    const std::string validationError =
        ValidateThemeBundle(bundle, options.strictness, report.unknownKeys);
    if (!validationError.empty()) {
        report.error = validationError;
        return report;
    }

    // App sections nothing has registered for are reported, not fatal — that is
    // what lets another app's theme contribute its colours and fonts here.
    for (const auto& [name, body] : bundle.appSections) {
        if (!FindThemeBundleSection(name)) {
            report.skippedSections.push_back(name);
        }
    }

    // ---- counts, for the report (and so a dry run has something to say) ----
    report.assetChanges = static_cast<int>(bundle.assets.size());
    report.fontFamilyChanges = static_cast<int>(bundle.fonts.families.size());

    std::vector<std::pair<std::string, std::string>> secretWrites;
    // Content. In REPLACE mode every registered key is written, including the
    // ones the bundle omitted — writing "" is how a key returns to its default.
    std::vector<std::string> contentKeys{std::string(Secrets::kSiteLogoUrl)};
    for (const ContentSlot& slot : SiteContentSlots()) {
        contentKeys.push_back(std::string(slot.key));
    }
    for (const std::string& key : contentKeys) {
        auto it = bundle.content.find(key);
        const bool present = it != bundle.content.end();
        if (!present && options.merge) {
            continue;  // merge leaves what it does not mention alone
        }
        std::string value = present ? NormalizeLineEndings(it->second) : std::string();
        // A bundled file becomes the route that will serve it. The asset itself
        // is placed below; this is only the reference.
        if (!value.empty() && IsUrlSlot(key) && IsBundleAssetReference(value)) {
            value = std::string(kThemeBundleAssetUrlPrefix) + value;
        }
        secretWrites.emplace_back(key, value);
    }
    report.contentChanges = static_cast<int>(secretWrites.size());

    const std::size_t contentCount = secretWrites.size();
    for (const ThemeToken& token : SiteThemeTokens()) {
        auto it = bundle.tokens.find(std::string(token.key));
        const bool present = it != bundle.tokens.end();
        if (!present && options.merge) {
            continue;
        }
        secretWrites.emplace_back(
            std::string(token.key), present ? it->second : std::string());
    }
    report.tokenChanges = static_cast<int>(secretWrites.size() - contentCount);

    if (options.dryRun) {
        report.ok = true;
        return report;
    }

    // ---- apply ----
    //
    // Images first: a slot value written below points at one, so the row has to
    // exist before anything refers to it.
    TableHelpers::SiteAssets siteAssets(databaseHelper);
    std::set<std::string> keptAssets;
    for (const auto& [name, bytes] : bundle.assets) {
        // Fonts live in site_font_faces, not here — only the images a URL slot
        // can point at become site assets.
        if (FontFormatFromMagicBytes(bytes).empty()) {
            siteAssets.PutAsset(transaction, name, ImageTypeFromMagicBytes(bytes),
                                bytes);
            keptAssets.insert(name);
        }
    }
    if (!options.merge) {
        for (const KeyValueTable& row : siteAssets.GetAllAssets(transaction)) {
            const std::string name = ValueOr(row, DbSchema::kSiteAssetName);
            if (!keptAssets.count(name)) {
                siteAssets.DeleteAssetByName(transaction, name);
            }
        }
    }

    for (const auto& [key, value] : secretWrites) {
        secrets.AddSecret(transaction, key, value);
    }

    TableHelpers::SiteFonts fonts(databaseHelper);

    // Sources: reconcile by key, exactly as the manage endpoint does, so a
    // family keeps pointing at the same row.
    std::map<std::string, int64_t> sourceIdByKey;
    std::set<std::string> keptSourceKeys;
    for (const BundleFontSource& source : bundle.fonts.sources) {
        std::vector<std::string> preconnectLines;
        for (const BundleFontPreconnect& preconnect : source.preconnects) {
            preconnectLines.push_back(
                preconnect.url + "|" + (preconnect.crossorigin ? "true" : "false"));
        }
        const std::string packed = PackLines(preconnectLines);
        KeyValueTable existing =
            fonts.GetSourceByKey(transaction, source.sourceKey);
        if (existing.empty()) {
            sourceIdByKey[source.sourceKey] = fonts.AddSource(
                transaction, source.sourceKey, source.displayName,
                source.baseUrl, source.querySuffix, packed);
        } else {
            const int64_t id = std::atoll(
                ValueOr(existing, DbSchema::kSiteFontSourceId).c_str());
            fonts.UpdateSource(transaction, id, source.sourceKey,
                               source.displayName, source.baseUrl,
                               source.querySuffix, packed);
            sourceIdByKey[source.sourceKey] = id;
        }
        keptSourceKeys.insert(source.sourceKey);
    }

    int ordinal = 10;
    std::set<std::string> keptFamilies;
    for (const BundleFontFamily& family : bundle.fonts.families) {
        int64_t sourceId = 0;
        std::string spec;
        if (family.sourceKind == DbSchema::kSiteFontSourceKindCdn) {
            auto it = sourceIdByKey.find(family.sourceKey);
            sourceId = it == sourceIdByKey.end() ? 0 : it->second;
            spec = family.spec;
        }
        KeyValueTable existing = fonts.GetFontByFamily(transaction, family.family);
        int64_t fontId = 0;
        if (existing.empty()) {
            fontId = fonts.AddFont(transaction, family.family, family.fallback,
                                   family.sourceKind, sourceId, spec, ordinal);
        } else {
            fontId = std::atoll(ValueOr(existing, DbSchema::kSiteFontId).c_str());
            fonts.UpdateFont(transaction, fontId, family.family, family.fallback,
                             family.sourceKind, sourceId, spec, ordinal);
        }
        keptFamilies.insert(family.family);
        ordinal += 10;

        // Faces are REPLACED from the bundle: the bundle is the statement of
        // what this family is, and a leftover face from the previous theme
        // would render at a weight the new theme never chose.
        for (const KeyValueTable& faceRow :
             fonts.GetFacesForFont(transaction, fontId)) {
            fonts.DeleteFace(transaction, std::atoll(
                ValueOr(faceRow, DbSchema::kSiteFontFaceId).c_str()));
        }
        for (const BundleFontFace& face : family.faces) {
            auto asset = bundle.assets.find(face.file);
            if (asset == bundle.assets.end()) {
                continue;  // validation already guaranteed this
            }
            fonts.AddFace(transaction, fontId, face.weight,
                          face.style == "italic" ? "italic" : "normal",
                          FontFormatFromMagicBytes(asset->second), asset->second);
        }
    }

    if (!options.merge) {
        // Replace: anything the bundle did not list goes, faces before families
        // and families before sources.
        for (const KeyValueTable& row : fonts.GetAllFonts(transaction)) {
            const std::string name = ValueOr(row, DbSchema::kSiteFontFamily);
            if (keptFamilies.count(name)) {
                continue;
            }
            const int64_t id = std::atoll(
                ValueOr(row, DbSchema::kSiteFontId).c_str());
            for (const KeyValueTable& faceRow :
                 fonts.GetFacesForFont(transaction, id)) {
                fonts.DeleteFace(transaction, std::atoll(
                    ValueOr(faceRow, DbSchema::kSiteFontFaceId).c_str()));
            }
            fonts.DeleteFont(transaction, id);
        }
        for (const KeyValueTable& row : fonts.GetAllSources(transaction)) {
            if (keptSourceKeys.count(ValueOr(row, DbSchema::kSiteFontSourceKey))) {
                continue;
            }
            fonts.DeleteSource(transaction, std::atoll(
                ValueOr(row, DbSchema::kSiteFontSourceId).c_str()));
        }
    }

    // ---- app sections ----
    SectionContext context;
    context.databaseHelper = &databaseHelper;
    context.transaction = &transaction;
    context.bundle = &bundle;
    for (const auto& [name, body] : bundle.appSections) {
        const ThemeBundleSection* section = FindThemeBundleSection(name);
        if (!section) {
            continue;  // already reported as skipped
        }
        const std::string reason = section->importer(context, body, options.merge);
        if (!reason.empty()) {
            // The caller runs this inside one transaction, so throwing here
            // rolls the whole import back rather than leaving half a theme.
            report.error = reason;
            return report;
        }
    }

    report.ok = true;
    return report;
}

}  // namespace Branding
