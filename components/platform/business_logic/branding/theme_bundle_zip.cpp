#include "business_logic/branding/theme_bundle_zip.h"

#include <vector>

#include <zip.h>

#include "business_logic/branding/theme_bundle_assets.h"
#include "business_logic/branding/theme_bundle_json.h"
#include "util/types.h"

namespace Branding {
namespace {

// RAII for the two libzip handles, so every early return below cleans up.
struct ZipSourceGuard {
    zip_source_t* source = nullptr;
    ~ZipSourceGuard() {
        if (source) zip_source_free(source);
    }
};

struct ZipGuard {
    zip_t* archive = nullptr;
    ~ZipGuard() {
        if (archive) zip_discard(archive);
    }
};

}  // namespace

std::string ThemeBundleToZip(const ThemeBundle& bundle) {
    zip_error_t error;
    zip_error_init(&error);
    zip_source_t* source = zip_source_buffer_create(nullptr, 0, 0, &error);
    if (!source) {
        zip_error_fini(&error);
        return {};
    }
    // The archive takes ownership on success; keep it alive past zip_close so
    // the bytes can be read back out.
    zip_source_keep(source);

    zip_t* archive = zip_open_from_source(source, ZIP_TRUNCATE, &error);
    if (!archive) {
        zip_source_free(source);
        zip_error_fini(&error);
        return {};
    }
    zip_error_fini(&error);

    // Entry payloads must outlive zip_close(), which is when libzip actually
    // reads them — a temporary here would be a dangling pointer at write time.
    std::vector<std::string> held;
    held.reserve(bundle.assets.size() + 1);

    auto addEntry = [&](const std::string& name, const std::string& bytes) -> bool {
        held.push_back(bytes);
        const std::string& stored = held.back();
        zip_source_t* entry = zip_source_buffer(
            archive, stored.data(), stored.size(), 0);
        if (!entry) {
            return false;
        }
        if (zip_file_add(archive, name.c_str(), entry, ZIP_FL_ENC_UTF_8) < 0) {
            zip_source_free(entry);
            return false;
        }
        return true;
    };

    // theme.json first, so opening the archive shows it at the top.
    if (!addEntry(std::string(kThemeBundleJsonName),
                  ThemeBundleToJson(bundle).ToString())) {
        zip_discard(archive);
        zip_source_free(source);
        return {};
    }
    for (const auto& [name, bytes] : bundle.assets) {
        if (!IsValidBundleAssetName(name)) {
            zip_discard(archive);
            zip_source_free(source);
            return {};
        }
        // Images and fonts are already compressed; deflating them again costs
        // time and saves nothing.
        if (!addEntry(name, bytes)) {
            zip_discard(archive);
            zip_source_free(source);
            return {};
        }
        zip_set_file_compression(
            archive, zip_name_locate(archive, name.c_str(), 0), ZIP_CM_STORE, 0);
    }

    if (zip_close(archive) < 0) {
        zip_discard(archive);
        zip_source_free(source);
        return {};
    }

    // Read the finished archive back out of the in-memory source.
    //
    // A read LOOP rather than one stat-sized read: zip_source_stat on a source
    // that has just been written through is not required to report a usable
    // size, and sizing a buffer from it is how you get a wrong-length read.
    // Looping until zip_source_read returns 0 is libzip's documented pattern
    // and needs no size up front.
    std::string result;
    if (zip_source_open(source) == 0) {
        char buffer[64 * 1024];
        for (;;) {
            const zip_int64_t read = zip_source_read(source, buffer, sizeof(buffer));
            if (read < 0) {
                result.clear();
                break;
            }
            if (read == 0) {
                break;
            }
            result.append(buffer, static_cast<std::size_t>(read));
        }
        zip_source_close(source);
    }
    zip_source_free(source);
    return result;
}

std::string ThemeBundleFromZip(
    std::string_view zipBytes,
    Json::Value& jsonOut,
    std::map<std::string, std::string>& assetsOut) {

    if (zipBytes.empty()) {
        return "That file is empty.";
    }
    zip_error_t error;
    zip_error_init(&error);
    zip_source_t* source = zip_source_buffer_create(
        zipBytes.data(), zipBytes.size(), 0, &error);
    if (!source) {
        zip_error_fini(&error);
        return "That file could not be read.";
    }
    ZipGuard archive;
    archive.archive = zip_open_from_source(source, ZIP_RDONLY, &error);
    if (!archive.archive) {
        zip_source_free(source);
        zip_error_fini(&error);
        // A truncated or non-zip upload lands here — a bad file, not a crash.
        return "That file is not a theme file (it is not a .zip).";
    }
    zip_error_fini(&error);

    const zip_int64_t entryCount = zip_get_num_entries(archive.archive, 0);
    if (entryCount < 0) {
        return "That theme file could not be read.";
    }
    // +1 for theme.json itself.
    if (static_cast<std::size_t>(entryCount) > kMaxBundleAssets + 1) {
        return "That theme file has more files in it than we can accept.";
    }

    std::string jsonText;
    std::vector<std::string> names;
    std::size_t totalBytes = 0;

    for (zip_int64_t index = 0; index < entryCount; ++index) {
        zip_stat_t stat;
        zip_stat_init(&stat);
        if (zip_stat_index(archive.archive, index, 0, &stat) != 0) {
            return "That theme file could not be read.";
        }
        const std::string name = stat.name ? stat.name : "";
        // A directory entry, or anything that is not a bare filename. This is
        // the zip-slip guard: `../../etc/passwd` simply is not a valid name, so
        // it is refused for its SHAPE rather than resolved and then checked.
        if (name.empty() || name.back() == '/') {
            return "That theme file contains folders, which a theme cannot have.";
        }
        if (name != kThemeBundleJsonName && !IsValidBundleAssetName(name)) {
            return "\"" + name + "\" is not a usable file name.";
        }
        if (!(stat.valid & ZIP_STAT_SIZE)) {
            return "That theme file could not be read.";
        }
        const std::size_t size = static_cast<std::size_t>(stat.size);
        const std::size_t cap = name == kThemeBundleJsonName
                                    ? kMaxBundleJsonBytes
                                    : kMaxBundleAssetBytes;
        // Checked BEFORE decompressing: a zip bomb is refused rather than
        // expanded into memory and then measured.
        if (size > cap) {
            return "\"" + name + "\" is larger than we can accept.";
        }
        totalBytes += size;
        if (totalBytes > kMaxBundleTotalBytes) {
            return "That theme file's contents add up to more than we can accept.";
        }

        zip_file_t* file = zip_fopen_index(archive.archive, index, 0);
        if (!file) {
            return "\"" + name + "\" could not be read.";
        }
        std::string bytes;
        bytes.resize(size);
        const zip_int64_t read =
            size == 0 ? 0 : zip_fread(file, bytes.data(), size);
        zip_fclose(file);
        if (read < 0 || static_cast<std::size_t>(read) != size) {
            return "\"" + name + "\" could not be read.";
        }

        if (name == kThemeBundleJsonName) {
            jsonText = std::move(bytes);
        } else {
            names.push_back(name);
            assetsOut[name] = std::move(bytes);
        }
    }

    if (jsonText.empty()) {
        return "That theme file has no theme.json in it.";
    }
    const std::string duplicate = FindCaseInsensitiveDuplicate(names);
    if (!duplicate.empty()) {
        return "Two files in that theme are both called \"" + duplicate + "\".";
    }

    jsonOut = Json::Value::FromText(jsonText);
    if (!jsonOut.HasChildren()) {
        return "The theme.json in that file could not be read.";
    }
    return {};
}

}  // namespace Branding
