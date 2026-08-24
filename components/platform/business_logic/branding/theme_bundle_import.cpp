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
#include "sql_util/database_access/database_metadata.h"
#include "sql_util/database_access/db_and_table_operations.h"
#include "sql_util/schema/database_info.h"
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

void AddProblem(
    std::vector<BundleProblem>& problems,
    std::string_view area,
    std::string_view item,
    std::string reason) {
    problems.push_back(
        BundleProblem{std::string(area), std::string(item), std::move(reason)});
}

// Makes sure the framework tables this import writes to actually exist,
// CREATING any that do not.
//
// The images are in the zip. A studio who opens Site Theme, picks their theme
// file and presses Apply has said everything they need to say — being told
// afterwards that the pictures were dropped and to go and run a command-line
// tool is not an acceptable answer, however politely it is phrased. So the
// import brings the database up to the shape it needs rather than reporting
// that it is the wrong shape.
//
// This is NOT a schema back-door. The DDL comes from the SAME db_schema
// builders that MakeFrameworkTables and framework migration 0001_site_assets
// use, so there is exactly one definition of each table; this only decides
// WHEN it runs. Postgres DDL is transactional, so a create here lives or dies
// with the rest of the import.
//
// Ordered by foreign key: sources before fonts before faces, matching
// MakeFrameworkTables.
void EnsureBundleStorage(Transaction& transaction) {
    std::set<std::string> tables;
    for (const std::string& table : DbMeta::ListTables(transaction)) {
        tables.insert(table);
    }

    DbSchema::DatabaseInfo databaseInfo("");
    DbSchema::MakeSiteFontSourcesTable(databaseInfo);
    DbSchema::MakeSiteFontsTable(databaseInfo);
    DbSchema::MakeSiteFontFacesTable(databaseInfo);
    DbSchema::MakeSiteAssetsTable(databaseInfo);

    for (std::string_view table : { DbSchema::kSiteFontSources,
                                    DbSchema::kSiteFonts,
                                    DbSchema::kSiteFontFaces,
                                    DbSchema::kSiteAssets }) {
        if (!tables.count(std::string(table))) {
            DbOps::CreateTable(transaction, databaseInfo, table);
        }
    }
}

// The font half of an apply. Extracted to keep ImportThemeBundleJson readable
// rather than carrying a hundred more lines inline.
//
// Everything here assumes the bundle has already been pruned: a family that
// survived PruneUnusableBundleItems has a valid name, a fallback, and — if it
// is an uploaded family — at least one face whose bytes really are a font.
void ApplyBundleFonts(
    DatabaseHelper databaseHelper,
    Transaction& transaction,
    const ThemeBundle& bundle,
    bool merge) {

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
                continue;  // pruning already guaranteed this
            }
            fonts.AddFace(transaction, fontId, face.weight,
                          face.style == "italic" ? "italic" : "normal",
                          FontFormatFromMagicBytes(asset->second), asset->second);
        }
    }

    if (!merge) {
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
}

}  // namespace

std::string FindFatalBundleProblem(const ThemeBundle& bundle) {
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
    // config_secrets holds live credentials, so a key outside the `site_`
    // namespace is refused however tolerant we are: "lenient" must never become
    // the way a mail password arrives in something labelled a theme.
    for (const auto& [key, value] : bundle.content) {
        if (key != Secrets::kSiteLogoUrl && !FindContentSlot(key) &&
            key.rfind("site_", 0) != 0) {
            return "\"" + key + "\" is not a theme setting.";
        }
    }
    for (const auto& [key, value] : bundle.tokens) {
        if (!FindThemeToken(key) && key.rfind("site_", 0) != 0) {
            return "\"" + key + "\" is not a theme setting.";
        }
    }
    return {};
}

void PruneUnusableBundleItems(
    ThemeBundle& bundle,
    std::vector<std::string>& unknownKeysOut,
    std::vector<BundleProblem>& problemsOut) {

    // ---- content ----
    for (auto it = bundle.content.begin(); it != bundle.content.end();) {
        const std::string& key = it->first;
        const std::string& value = it->second;
        if (key != Secrets::kSiteLogoUrl && !FindContentSlot(key)) {
            // Unrecognised but harmless — reported, and dropped so it cannot be
            // written into config_secrets as a setting nothing reads.
            unknownKeysOut.push_back(key);
            it = bundle.content.erase(it);
            continue;
        }
        if (value.empty()) {
            ++it;  // empty means "use the default"
            continue;
        }
        if (IsUrlSlot(key) && IsBundleAssetReference(value)) {
            if (!bundle.HasAsset(value)) {
                AddProblem(problemsOut, "content", key,
                           "points at \"" + value +
                               "\", which is not in that theme file.");
                it = bundle.content.erase(it);
                continue;
            }
            ++it;
            continue;
        }
        const ContentSlot* slot = FindContentSlot(key);
        if (slot) {
            const std::string reason =
                ValidateSlotValue(slot->type, NormalizeLineEndings(value));
            if (!reason.empty()) {
                AddProblem(problemsOut, "content", key, reason);
                it = bundle.content.erase(it);
                continue;
            }
        }
        ++it;
    }

    // ---- tokens ----
    for (auto it = bundle.tokens.begin(); it != bundle.tokens.end();) {
        const ThemeToken* token = FindThemeToken(it->first);
        if (!token) {
            unknownKeysOut.push_back(it->first);
            it = bundle.tokens.erase(it);
            continue;
        }
        if (it->second.empty()) {
            ++it;  // clearing a token IS "reset to default"
            continue;
        }
        if (!IsValidThemeTokenValue(token->type, it->second)) {
            AddProblem(problemsOut, "tokens", it->first,
                       "\"" + it->second + "\" is not a valid value for that "
                       "setting.");
            it = bundle.tokens.erase(it);
            continue;
        }
        ++it;
    }

    // ---- font sources ----
    //
    // Sources are pruned before families, because a family naming a source that
    // was just dropped has to go with it.
    std::set<std::string> sourceKeys;
    std::vector<BundleFontSource> keptSources;
    for (const BundleFontSource& source : bundle.fonts.sources) {
        std::string reason;
        if (source.sourceKey.empty()) {
            reason = "a font service in that theme has no short key.";
        } else if (!sourceKeys.insert(source.sourceKey).second) {
            reason = "two font services in that theme share this key.";
        } else if (!IsValidFontSourceUrl(source.baseUrl)) {
            reason = "the font service address must be an https:// URL.";
        } else if (!source.querySuffix.empty() &&
                   !IsValidFontSpec(source.querySuffix)) {
            reason = "that extra setting is not valid.";
        }
        if (reason.empty()) {
            for (const BundleFontPreconnect& preconnect : source.preconnects) {
                if (!IsValidFontSourceUrl(preconnect.url)) {
                    reason = "\"" + preconnect.url +
                             "\" is not a usable address to warm up.";
                    break;
                }
            }
        }
        if (!reason.empty()) {
            sourceKeys.erase(source.sourceKey);
            AddProblem(problemsOut, "fonts", source.sourceKey, reason);
            continue;
        }
        keptSources.push_back(source);
    }
    bundle.fonts.sources = keptSources;

    // ---- font families ----
    std::set<std::string> familyNames;
    std::vector<BundleFontFamily> keptFamilies;
    for (BundleFontFamily family : bundle.fonts.families) {
        std::string reason;
        if (!IsValidFontFamilyName(family.family)) {
            reason = "that is not a usable font name.";
        } else if (!familyNames.insert(family.family).second) {
            reason = "two fonts in that theme are both called this.";
        } else if (family.fallback.empty() ||
                   !IsValidFontFamilyList(family.fallback)) {
            // D13: the fallback is required and never assumed.
            reason = "needs a backup font (for example sans-serif).";
        } else if (family.sourceKind == DbSchema::kSiteFontSourceKindCdn) {
            if (!sourceKeys.count(family.sourceKey)) {
                reason = "names a font service (\"" + family.sourceKey +
                         "\") that theme does not include.";
            } else if (!IsValidFontSpec(family.spec)) {
                reason = "that font specification is not valid.";
            }
        } else if (family.sourceKind == DbSchema::kSiteFontSourceKindUploaded) {
            // A face whose file is missing or is not really a font drops on its
            // own; the family survives on whatever faces are left.
            std::vector<BundleFontFace> keptFaces;
            for (const BundleFontFace& face : family.faces) {
                std::string faceReason;
                if (face.weight < 1 || face.weight > 1000) {
                    faceReason = StringFromInt(face.weight) +
                                 " is not a usable font weight.";
                } else {
                    auto asset = bundle.assets.find(face.file);
                    if (asset == bundle.assets.end()) {
                        faceReason = "the file \"" + face.file +
                                     "\" is not in that theme file.";
                    } else if (FontFormatFromMagicBytes(asset->second).empty()) {
                        // D14: the format comes from the MAGIC BYTES, never the
                        // name. This is what stops a renamed .woff2 that is
                        // really HTML being stored and served from our origin.
                        faceReason = "\"" + face.file +
                                     "\" is not a WOFF2, WOFF, TTF or OTF font.";
                    }
                }
                if (faceReason.empty()) {
                    keptFaces.push_back(face);
                } else {
                    AddProblem(problemsOut, "fonts", family.family, faceReason);
                }
            }
            family.faces = keptFaces;
            if (family.faces.empty()) {
                reason = "has no usable font files, so nothing could be shown "
                         "in it.";
            }
        } else if (family.sourceKind != DbSchema::kSiteFontSourceKindSystem) {
            reason = "unknown font kind \"" + family.sourceKind + "\".";
        }
        if (!reason.empty()) {
            familyNames.erase(family.family);
            AddProblem(problemsOut, "fonts", family.family, reason);
            continue;
        }
        keptFamilies.push_back(family);
    }
    bundle.fonts.families = keptFamilies;
}

std::string ValidateThemeBundle(
    const ThemeBundle& bundle,
    BundleStrictness strictness,
    std::vector<std::string>& unknownKeysOut) {

    // Expressed in terms of the two functions the importer uses, so there is
    // ONE definition of what makes a bundle item unusable. This used to be a
    // second, parallel copy of every check; the two drifted the moment either
    // was edited.
    const std::string fatal = FindFatalBundleProblem(bundle);
    if (!fatal.empty()) {
        return fatal;
    }
    ThemeBundle pruned = bundle;
    std::vector<BundleProblem> problems;
    PruneUnusableBundleItems(pruned, unknownKeysOut, problems);
    if (!problems.empty()) {
        const BundleProblem& first = problems.front();
        return first.item.empty() ? first.reason
                                  : first.item + ": " + first.reason;
    }
    if (strictness == BundleStrictness::Strict && !unknownKeysOut.empty()) {
        return "That theme file has settings this site does not have (" +
               unknownKeysOut.front() +
               (unknownKeysOut.size() > 1
                    ? " and " + StringFromInt(static_cast<int64_t>(
                                    unknownKeysOut.size() - 1)) + " more"
                    : "") + ").";
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

    // 3. Refuse only what is unusable however tolerant we are, then PRUNE the
    //    rest. Everything that survives is applied; everything that did not is
    //    in report.problems with a reason attached to it.
    const std::string fatal = FindFatalBundleProblem(bundle);
    if (!fatal.empty()) {
        report.error = fatal;
        return report;
    }
    PruneUnusableBundleItems(bundle, report.unknownKeys, report.problems);

    // 4. Make sure the tables this import writes to exist, creating any that
    //    do not. Done BEFORE the first write: a query against a missing table
    //    throws, and inside the one transaction an import runs in that poisons
    //    everything after it — which is how this surfaced originally, as a 500
    //    with a SQL fragment in the log and nothing on screen.
    //
    //    Skipped on a dry run, which must not write anything at all. That is
    //    safe because the apply does it before touching either area.
    if (!options.dryRun) {
        EnsureBundleStorage(transaction);
    }

    // App sections nothing has registered for are reported, not fatal — that is
    // what lets another app's theme contribute its colours and fonts here.
    for (const auto& [name, body] : bundle.appSections) {
        if (!FindThemeBundleSection(name)) {
            report.skippedSections.push_back(name);
        }
    }

    // STRICT means "refuse anything I do not fully understand", so under it the
    // first problem becomes the refusal. LENIENT — the default — applies what
    // it understands and reports the rest. Both modes report the same list; the
    // only difference is whether it stops the import.
    if (options.strictness == BundleStrictness::Strict) {
        if (!report.unknownKeys.empty()) {
            report.error =
                "That theme file has settings this site does not have (" +
                report.unknownKeys.front() +
                (report.unknownKeys.size() > 1
                     ? " and " + StringFromInt(static_cast<int64_t>(
                                     report.unknownKeys.size() - 1)) + " more"
                     : "") + ").";
            return report;
        }
        if (!report.problems.empty()) {
            const BundleProblem& first = report.problems.front();
            report.error = first.item.empty()
                               ? first.reason
                               : first.item + ": " + first.reason;
            return report;
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
    {
        TableHelpers::SiteAssets siteAssets(databaseHelper);
        std::set<std::string> keptAssets;
        for (const auto& [name, bytes] : bundle.assets) {
            // Fonts live in site_font_faces, not here — only the images a URL
            // slot can point at become site assets.
            if (FontFormatFromMagicBytes(bytes).empty()) {
                siteAssets.PutAsset(
                    transaction, name, ImageTypeFromMagicBytes(bytes), bytes);
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
    }

    for (const auto& [key, value] : secretWrites) {
        secrets.AddSecret(transaction, key, value);
    }

    ApplyBundleFonts(databaseHelper, transaction, bundle, options.merge);

    // ---- app sections ----
    SectionContext context;
    context.databaseHelper = &databaseHelper;
    context.transaction = &transaction;
    context.bundle = &bundle;
    // Sections report their own skipped rows straight onto the report, so one
    // bad home-page row does not have to take the theme down with it.
    context.problems = &report.problems;
    for (const auto& [name, body] : bundle.appSections) {
        const ThemeBundleSection* section = FindThemeBundleSection(name);
        if (!section) {
            continue;  // already reported as skipped
        }
        const std::string reason = section->importer(context, body, options.merge);
        if (reason.empty()) {
            continue;
        }
        // A returned reason now means "this section could not be applied AT
        // ALL" — anything finer-grained goes through AddSectionProblem. Under
        // Strict that refuses the import (the caller's transaction rolls back).
        // Under Lenient the rest of the theme still lands and the section is
        // reported, because a studio importing a sibling app's theme should get
        // its colours even if its page content does not fit.
        if (options.strictness == BundleStrictness::Strict) {
            report.error = reason;
            return report;
        }
        AddProblem(report.problems, "section:" + name, "", reason);
    }

    report.ok = true;
    return report;
}

}  // namespace Branding
