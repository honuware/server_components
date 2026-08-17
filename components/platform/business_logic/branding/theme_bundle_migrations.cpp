#include "business_logic/branding/theme_bundle_migrations.h"

#include "util/types.h"

namespace Branding {
namespace {

// v1 is the first published format, so the chain is empty today.
//
// The machinery exists anyway, deliberately: the first token rename must be a
// DATA change (append a step here) rather than a redesign under time pressure
// with real theme files already in the wild. See theme_bundle_migrations_test
// for a worked example of what a step looks like.
//
// To add one:
//   1. bump kCurrentFormatVersion,
//   2. append {oldVersion, newVersion, "what changed", [](Json::Value& json){…}},
//   3. add a test that migrates a fixture written in the old shape.
constexpr int kCurrentFormatVersion = 1;
constexpr int kOldestSupportedFormatVersion = 1;

}  // namespace

int CurrentBundleFormatVersion() { return kCurrentFormatVersion; }

int OldestSupportedBundleFormatVersion() { return kOldestSupportedFormatVersion; }

const std::vector<BundleMigration>& BundleMigrations() {
    static const std::vector<BundleMigration> migrations = {
        // (empty — see the note above)
    };
    return migrations;
}

std::string MigrateBundleJson(
    Json::Value& json, std::vector<std::string>& appliedOut) {
    const Json::Value* versionField = nullptr;
    int version = 0;
    if (json.HasChild("format_version", &versionField)) {
        if (const int64_t* number = versionField->TryGet<int64_t>()) {
            version = static_cast<int>(*number);
        } else if (const std::string* text = versionField->TryGet<std::string>()) {
            version = std::atoi(text->c_str());
        }
    }
    if (version <= 0) {
        return "That theme file does not say which format version it is.";
    }
    if (version > kCurrentFormatVersion) {
        // Refused rather than attempted. A newer bundle may MEAN things this
        // build cannot honour, and applying the parts we recognise would
        // produce a look its author never approved.
        return "That theme file was made by a newer version of this site "
               "(format " + StringFromInt(version) + ", this build understands " +
               StringFromInt(kCurrentFormatVersion) + ").";
    }
    if (version < kOldestSupportedFormatVersion) {
        return "That theme file is too old to be read (format " +
               StringFromInt(version) + ").";
    }

    for (const BundleMigration& migration : BundleMigrations()) {
        if (migration.from != version) {
            continue;
        }
        migration.apply(json);
        appliedOut.push_back(migration.description);
        version = migration.to;
    }

    if (version != kCurrentFormatVersion) {
        // A gap in the chain. This is a build bug, not a bad bundle, so it says
        // so rather than blaming the studio's file.
        return "This build cannot migrate that theme file (stopped at format " +
               StringFromInt(version) + ").";
    }
    json["format_version"] = Json::Value(static_cast<int64_t>(version));
    return {};
}

Json::Value BundleImportReportToJson(const BundleImportReport& report) {
    Json::JsonArray migrations;
    for (const std::string& description : report.migrationsApplied) {
        migrations.push_back(Json::Value(description));
    }
    Json::JsonArray unknown;
    for (const std::string& key : report.unknownKeys) {
        unknown.push_back(Json::Value(key));
    }
    Json::JsonArray skipped;
    for (const std::string& section : report.skippedSections) {
        skipped.push_back(Json::Value(section));
    }
    Json::JsonObject object{
        {"ok", Json::Value(report.ok)},
        {"migrated_from", Json::Value(static_cast<int64_t>(report.migratedFrom))},
        {"migrations_applied", Json::Value(migrations)},
        {"unknown_keys", Json::Value(unknown)},
        {"skipped_sections", Json::Value(skipped)},
        {"changes", Json::Value(Json::JsonObject{
            {"content", Json::Value(static_cast<int64_t>(report.contentChanges))},
            {"tokens", Json::Value(static_cast<int64_t>(report.tokenChanges))},
            {"font_families",
             Json::Value(static_cast<int64_t>(report.fontFamilyChanges))},
            {"assets", Json::Value(static_cast<int64_t>(report.assetChanges))},
        })},
    };
    if (!report.error.empty()) {
        object["error"] = Json::Value(report.error);
    }
    return Json::Value(object);
}

}  // namespace Branding
