#include "business_logic/branding/theme_bundle_assets.h"

#include <algorithm>
#include <set>
#include <string>

namespace Branding {
namespace {

bool IsAsciiAlnum(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9');
}

bool IsAssetNameChar(char c) {
    return IsAsciiAlnum(c) || c == '.' || c == '_' || c == '-';
}

std::string ToLower(std::string_view value) {
    std::string lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    return lower;
}

}  // namespace

bool IsValidBundleAssetName(std::string_view name) {
    if (name.empty() || name.size() > kMaxAssetNameBytes) {
        return false;
    }
    // A leading `.`/`-`/`_` is what `.`, `..`, dotfiles and flag-shaped names
    // all start with, so requiring alphanumeric here rules out the whole family.
    if (!IsAsciiAlnum(name.front())) {
        return false;
    }
    for (char c : name) {
        if (!IsAssetNameChar(c)) {
            return false;
        }
    }
    // Belt and braces: the leading-character rule already blocks `..`, but a
    // name like `a..b` is still a shape nobody needs and traversal bugs love.
    if (name.find("..") != std::string_view::npos) {
        return false;
    }
    return true;
}

bool IsExternalUrlValue(std::string_view value) {
    return value.find("://") != std::string_view::npos;
}

bool IsBundleAssetReference(std::string_view value) {
    if (value.empty() || IsExternalUrlValue(value)) {
        return false;
    }
    return IsValidBundleAssetName(value);
}

std::string FindCaseInsensitiveDuplicate(const std::vector<std::string>& names) {
    std::set<std::string> seen;
    for (const std::string& name : names) {
        if (!seen.insert(ToLower(name)).second) {
            return name;
        }
    }
    return {};
}

std::string ExtensionForImageType(std::string_view imageType) {
    // Photos are stored with the type the upload endpoint recorded, which is
    // the subtype rather than the full MIME type ("jpeg", not "image/jpeg").
    std::string type = ToLower(imageType);
    auto slash = type.rfind('/');
    if (slash != std::string::npos) {
        type = type.substr(slash + 1);
    }
    if (type == "jpeg" || type == "jpg") return "jpg";
    if (type == "png") return "png";
    if (type == "gif") return "gif";
    if (type == "webp") return "webp";
    if (type == "svg" || type == "svg+xml") return "svg";
    if (type == "tiff" || type == "tif") return "tif";
    return {};
}

std::string ExtensionForFontFormat(std::string_view fontFormat) {
    std::string format = ToLower(fontFormat);
    if (format == "woff2") return "woff2";
    if (format == "woff") return "woff";
    if (format == "ttf") return "ttf";
    if (format == "otf") return "otf";
    return {};
}

std::string ImageTypeFromMagicBytes(std::string_view bytes) {
    auto startsWith = [&](std::string_view prefix) {
        return bytes.size() >= prefix.size() &&
               bytes.compare(0, prefix.size(), prefix) == 0;
    };
    if (startsWith(std::string_view("\x89PNG\r\n\x1a\n", 8))) return "png";
    if (startsWith(std::string_view("\xff\xd8\xff", 3))) return "jpeg";
    if (startsWith("GIF87a") || startsWith("GIF89a")) return "gif";
    // RIFF....WEBP — the size field sits between the two tags.
    if (bytes.size() >= 12 && startsWith("RIFF") &&
        bytes.compare(8, 4, "WEBP") == 0) {
        return "webp";
    }
    // SVG is text, so there are no magic bytes to speak of — look for the root
    // element within the first chunk, past any XML declaration or comment.
    // Deliberately narrow: an HTML file must not pass as an "image".
    const std::string_view head = bytes.substr(0, std::min<std::size_t>(bytes.size(), 512));
    if (head.find("<svg") != std::string_view::npos) return "svg";
    return {};
}

std::string SanitizeAssetStem(std::string_view text) {
    std::string stem;
    bool lastWasSeparator = false;
    for (char c : ToLower(text)) {
        if (IsAsciiAlnum(c)) {
            stem += c;
            lastWasSeparator = false;
        } else if (!stem.empty() && !lastWasSeparator) {
            // Collapse runs of spaces/punctuation into a single hyphen, so
            // "Sunrise  Studio!" and "Sunrise-Studio" agree.
            stem += '-';
            lastWasSeparator = true;
        }
    }
    while (!stem.empty() && stem.back() == '-') {
        stem.pop_back();
    }
    return stem;
}

std::string MakeAssetName(std::string_view stem, std::string_view extension) {
    if (extension.empty()) {
        // No extension means the caller does not actually know what this file
        // is. Emitting `logo.` would produce a bundle nothing can open.
        return {};
    }
    std::string cleanStem = SanitizeAssetStem(stem);
    if (cleanStem.empty()) {
        cleanStem = "asset";
    }
    // Reserve room for the dot and the extension before truncating the stem.
    const std::size_t room = kMaxAssetNameBytes - extension.size() - 1;
    if (cleanStem.size() > room) {
        cleanStem.resize(room);
        while (!cleanStem.empty() && cleanStem.back() == '-') {
            cleanStem.pop_back();
        }
        if (cleanStem.empty()) {
            cleanStem = "asset";
        }
    }
    std::string name = cleanStem + "." + std::string(extension);
    return IsValidBundleAssetName(name) ? name : std::string();
}

}  // namespace Branding
