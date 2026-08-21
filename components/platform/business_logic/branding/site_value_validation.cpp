#include "business_logic/branding/site_value_validation.h"

#include "util/types.h"

namespace Branding {
namespace {

constexpr std::string_view kTooLongTemplate =
    "Value is longer than the {limit}-byte limit for this field.";
constexpr std::string_view kControlCharMessage =
    "Value contains a control character.";
constexpr std::string_view kMultiLineMessage =
    "Value must be a single line.";
constexpr std::string_view kBadUrlMessage =
    "Value must be an http(s) URL or a root-relative path beginning with '/'.";
constexpr std::string_view kBadColorMessage =
    "Value must be a color in #RRGGBB form.";
constexpr std::string_view kUnknownTypeMessage =
    "Field has no validation rule and cannot be accepted.";

bool IsHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

// Anything below space plus DEL. Deliberately byte-wise: UTF-8 continuation
// bytes are all >= 0x80, so multi-byte characters (the emoji in Knotty Yoga's
// tagline, the em dashes in its copy) pass through untouched.
bool IsControlByte(char c) {
    unsigned char byte = static_cast<unsigned char>(c);
    return byte < 0x20 || byte == 0x7F;
}

std::string TooLongMessage(std::size_t limit) {
    KeyValueTable params;
    params["limit"] = StringFromInt(static_cast<int64_t>(limit));
    return FormatString(kTooLongTemplate, params);
}

// Shared by Line/Lines/Markdown: rejects control bytes other than the ones the
// type legitimately carries.
bool HasDisallowedControlByte(std::string_view value, bool allowNewline,
                              bool allowTab) {
    for (char c : value) {
        if (!IsControlByte(c)) {
            continue;
        }
        if (allowNewline && c == '\n') {
            continue;
        }
        if (allowTab && c == '\t') {
            continue;
        }
        return true;
    }
    return false;
}

}  // namespace

bool IsValidHexColor(std::string_view value) {
    if (value.size() != 7 || value[0] != '#') {
        return false;
    }
    for (std::size_t i = 1; i < value.size(); ++i) {
        if (!IsHexDigit(value[i])) {
            return false;
        }
    }
    return true;
}

bool IsValidSiteUrl(std::string_view value) {
    if (value.size() > kMaxUrlBytes) {
        return false;
    }
    for (char c : value) {
        // Space is not a control byte but is just as unwelcome in a URL.
        if (IsControlByte(c) || c == ' ') {
            return false;
        }
    }
    if (value.rfind("http://", 0) == 0) {
        return value.size() > 7;
    }
    if (value.rfind("https://", 0) == 0) {
        return value.size() > 8;
    }
    // Root-relative. `//host/path` is protocol-relative — an off-origin fetch
    // wearing a relative path's clothes — so it is refused alongside `/\`,
    // which several browsers normalize the same way.
    if (!value.empty() && value[0] == '/') {
        return value.size() == 1 || (value[1] != '/' && value[1] != '\\');
    }
    return false;
}

bool IsValidCssLength(std::string_view value) {
    if (value.empty() || value.size() > 16) {
        return false;
    }
    if (value == "0") {
        return true;
    }
    std::size_t i = 0;
    bool sawDigit = false;
    while (i < value.size() && value[i] >= '0' && value[i] <= '9') {
        ++i;
        sawDigit = true;
    }
    if (i < value.size() && value[i] == '.') {
        ++i;
        while (i < value.size() && value[i] >= '0' && value[i] <= '9') {
            ++i;
            sawDigit = true;
        }
    }
    if (!sawDigit) {
        return false;
    }
    std::string_view unit = value.substr(i);
    // pt joined the set for the type-scale editor's rem/px/pt unit toggle
    // (Polish Phase 2) — without it a pt size would save and then be silently
    // dropped from site_info on the read path.
    return unit == "px" || unit == "rem" || unit == "em" || unit == "%" ||
           unit == "pt";
}

bool IsValidFontFamilyList(std::string_view value) {
    if (value.empty() || value.size() > 256) {
        return false;
    }
    int quotes = 0;
    for (char c : value) {
        if (c == '"') {
            ++quotes;
            continue;
        }
        bool allowed =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == ' ' || c == '-' || c == '_' || c == ',';
        if (!allowed) {
            return false;
        }
    }
    // An unbalanced quote would leave the declaration open.
    return quotes % 2 == 0;
}

std::string ValidateSlotValue(SlotType type, std::string_view value) {
    // Empty is always legal: it means "unset — fall back to the bundled
    // default", so clearing a field is how a tenant reverts one.
    if (value.empty()) {
        return {};
    }

    switch (type) {
        case SlotType::Line:
            if (value.size() > kMaxLineBytes) {
                return TooLongMessage(kMaxLineBytes);
            }
            for (char c : value) {
                if (c == '\n' || c == '\r') {
                    return std::string(kMultiLineMessage);
                }
            }
            if (HasDisallowedControlByte(value, false, false)) {
                return std::string(kControlCharMessage);
            }
            return {};

        case SlotType::Lines:
            if (value.size() > kMaxLinesBytes) {
                return TooLongMessage(kMaxLinesBytes);
            }
            if (HasDisallowedControlByte(value, true, false)) {
                return std::string(kControlCharMessage);
            }
            return {};

        case SlotType::Markdown:
            if (value.size() > kMaxMarkdownBytes) {
                return TooLongMessage(kMaxMarkdownBytes);
            }
            if (HasDisallowedControlByte(value, true, true)) {
                return std::string(kControlCharMessage);
            }
            return {};

        case SlotType::Url:
            if (value.size() > kMaxUrlBytes) {
                return TooLongMessage(kMaxUrlBytes);
            }
            if (!IsValidSiteUrl(value)) {
                return std::string(kBadUrlMessage);
            }
            return {};

        case SlotType::Color:
            if (!IsValidHexColor(value)) {
                return std::string(kBadColorMessage);
            }
            return {};
    }
    // Unreachable: every SlotType is handled above. Present so a future
    // enumerator fails closed (refused) rather than silently accepted.
    return std::string(kUnknownTypeMessage);
}

std::string NormalizeLineEndings(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\r') {
            // Swallow the LF of a CRLF pair; a lone CR still becomes one LF.
            if (i + 1 < value.size() && value[i + 1] == '\n') {
                ++i;
            }
            out.push_back('\n');
            continue;
        }
        out.push_back(value[i]);
    }
    return out;
}

std::string NormalizeSlotValue(SlotType type, std::string_view value) {
    std::string normalized = NormalizeLineEndings(value);
    if (!ValidateSlotValue(type, normalized).empty()) {
        return {};
    }
    return normalized;
}

}  // namespace Branding
