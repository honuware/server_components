#include "business_logic/branding/theme_bundle_directory.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include "business_logic/branding/theme_bundle_assets.h"
#include "business_logic/branding/theme_bundle_json.h"

namespace Branding {
namespace {

namespace fs = std::filesystem;

std::string WriteFileBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return "Could not write " + path.filename().string() + ".";
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        return "Could not write " + path.filename().string() + ".";
    }
    return {};
}

}  // namespace

std::string WriteThemeBundleDirectory(
    const ThemeBundle& bundle, const std::string& directory, bool force) {
    std::error_code ec;
    const fs::path root(directory);

    if (fs::exists(root, ec)) {
        if (!fs::is_directory(root, ec)) {
            return directory + " is not a directory.";
        }
        // Overwriting a theme because someone mistyped a path is not
        // recoverable — the previous one is simply gone.
        if (fs::exists(root / std::string(kThemeBundleJsonName), ec) && !force) {
            return directory + " already holds a theme. Use --force to replace it.";
        }
    } else {
        fs::create_directories(root, ec);
        if (ec) {
            return "Could not create " + directory + ".";
        }
    }

    const std::string reason = WriteFileBytes(
        root / std::string(kThemeBundleJsonName),
        ThemeBundleToJson(bundle).ToString());
    if (!reason.empty()) {
        return reason;
    }

    for (const auto& [name, bytes] : bundle.assets) {
        if (!IsValidBundleAssetName(name)) {
            return "\"" + name + "\" is not a usable file name.";
        }
        const std::string assetReason = WriteFileBytes(root / name, bytes);
        if (!assetReason.empty()) {
            return assetReason;
        }
    }
    return {};
}

std::string ReadThemeBundleDirectory(
    const std::string& directory,
    Json::Value& jsonOut,
    std::map<std::string, std::string>& assetsOut) {

    std::error_code ec;
    const fs::path root(directory);
    if (!fs::is_directory(root, ec)) {
        return directory + " is not a directory.";
    }

    std::string jsonText;
    std::vector<std::string> names;
    std::size_t totalBytes = 0;

    // Non-recursive on purpose: a bundle is flat, and descending would let two
    // files in different subdirectories collide on one asset name.
    for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
        if (ec) {
            return "Could not read " + directory + ".";
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        const bool isManifest = name == kThemeBundleJsonName;
        if (!isManifest && !IsValidBundleAssetName(name)) {
            return "\"" + name + "\" is not a usable file name.";
        }

        const std::uintmax_t size = fs::file_size(entry.path(), ec);
        if (ec) {
            return "Could not read " + name + ".";
        }
        const std::size_t cap =
            isManifest ? kMaxBundleJsonBytes : kMaxBundleAssetBytes;
        // Checked before reading, so an enormous file is refused rather than
        // pulled into memory and then measured.
        if (size > cap) {
            return "\"" + name + "\" is larger than we can accept.";
        }
        totalBytes += static_cast<std::size_t>(size);
        if (totalBytes > kMaxBundleTotalBytes) {
            return "That theme's files add up to more than we can accept.";
        }

        std::ifstream in(entry.path(), std::ios::binary);
        if (!in) {
            return "Could not read " + name + ".";
        }
        std::string bytes((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
        if (isManifest) {
            jsonText = std::move(bytes);
        } else {
            names.push_back(name);
            assetsOut[name] = std::move(bytes);
        }
    }

    if (jsonText.empty()) {
        return directory + " has no theme.json in it.";
    }
    if (assetsOut.size() > kMaxBundleAssets) {
        return "That theme has more files in it than we can accept.";
    }
    const std::string duplicate = FindCaseInsensitiveDuplicate(names);
    if (!duplicate.empty()) {
        return "Two files in that theme are both called \"" + duplicate + "\".";
    }

    jsonOut = Json::Value::FromText(jsonText);
    if (!jsonOut.HasChildren()) {
        return "The theme.json in " + directory + " could not be read.";
    }
    return {};
}

}  // namespace Branding
