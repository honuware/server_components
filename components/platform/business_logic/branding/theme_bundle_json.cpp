#include "business_logic/branding/theme_bundle_json.h"

#include <set>

#include "business_logic/branding/site_content_slots.h"
#include "business_logic/branding/site_theme_tokens.h"
#include "business_logic/branding/theme_bundle_migrations.h"
#include "util/secrets/secret_keys.h"
#include "util/types.h"

namespace Branding {
namespace {

// The content keys a bundle carries: every registered slot, plus site_logo_url.
//
// site_logo_url is not in SiteContentSlots() because /api/site_info serves it
// from its own secret rather than through the slot registry — but it is
// unquestionably part of a look (OQ-TF4), so the bundle carries it.
std::vector<std::string> BundleContentKeys() {
    std::vector<std::string> keys;
    keys.reserve(SiteContentSlots().size() + 1);
    keys.push_back(std::string(Secrets::kSiteLogoUrl));
    for (const ContentSlot& slot : SiteContentSlots()) {
        keys.push_back(std::string(slot.key));
    }
    return keys;
}

// Which content keys take a list shape in the file rather than a string.
bool IsLinesSlot(const std::string& key) {
    for (const ContentSlot& slot : SiteContentSlots()) {
        if (key == slot.key) {
            return slot.type == SlotType::Lines;
        }
    }
    return false;
}

// The one key whose `lines` entries carry a `label|url` convention. Special
// cased BY KEY rather than by a slot type, because it is genuinely one key —
// inventing a `LabelledLines` slot type for a single member would spread the
// special case across the validator, the editor and the read path instead of
// keeping it here.
bool IsLabelledLinksSlot(const std::string& key) {
    return key == Secrets::kSiteSocialLinks;
}

// A hand-edited theme file will get types wrong — a weight typed as "700"
// rather than 700, a flag as "true" rather than true. Every reader below is
// non-throwing (Get<T> throws std::bad_variant_access on a mismatch) and
// accepts the neighbouring spelling, because a studio should get a validation
// message about its theme, never a 500 from a JSON type.
std::string ValueAsString(const Json::Value& value) {
    if (const std::string* text = value.TryGet<std::string>()) {
        return *text;
    }
    if (const int64_t* number = value.TryGet<int64_t>()) {
        return StringFromInt(*number);
    }
    if (const bool* flag = value.TryGet<bool>()) {
        return *flag ? "true" : "false";
    }
    return {};
}

std::string FieldString(const Json::Value& object, const char* name) {
    const Json::Value* field = nullptr;
    if (!object.HasChild(name, &field)) {
        return {};
    }
    return ValueAsString(*field);
}

int FieldInt(const Json::Value& object, const char* name, int fallback) {
    const Json::Value* field = nullptr;
    if (!object.HasChild(name, &field)) {
        return fallback;
    }
    if (const int64_t* number = field->TryGet<int64_t>()) {
        return static_cast<int>(*number);
    }
    const std::string text = ValueAsString(*field);
    if (text.empty()) {
        return fallback;
    }
    return std::atoi(text.c_str());
}

bool FieldBool(const Json::Value& object, const char* name, bool fallback) {
    const Json::Value* field = nullptr;
    if (!object.HasChild(name, &field)) {
        return fallback;
    }
    if (const bool* flag = field->TryGet<bool>()) {
        return *flag;
    }
    // Postgres booleans arrive as "t"/"f" through the KeyValueTable layer, and
    // a hand-edited file may well say "true" — accept both rather than making
    // a studio guess which spelling this field wants.
    const std::string text = ValueAsString(*field);
    return text == "true" || text == "t" || text == "1";
}

}  // namespace

// ---- packing conversions ----------------------------------------------------

std::vector<std::string> UnpackLines(std::string_view stored) {
    std::vector<std::string> lines;
    if (stored.empty()) {
        // Deliberately empty, not {""} — a stored blank means "no lines", and
        // returning one empty entry would grow a blank line every round trip.
        return lines;
    }
    std::string normalized = NormalizeLineEndings(stored);
    std::size_t start = 0;
    while (start <= normalized.size()) {
        std::size_t end = normalized.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(normalized.substr(start));
            break;
        }
        lines.push_back(normalized.substr(start, end - start));
        start = end + 1;
    }
    // A trailing newline should not produce a trailing empty entry.
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    return lines;
}

std::string PackLines(const std::vector<std::string>& lines) {
    std::string packed;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            packed += '\n';
        }
        packed += lines[i];
    }
    return packed;
}

std::vector<BundleLabelledLink> UnpackLabelledLinks(std::string_view stored) {
    std::vector<BundleLabelledLink> links;
    for (const std::string& line : UnpackLines(stored)) {
        BundleLabelledLink link;
        std::size_t bar = line.find('|');
        if (bar == std::string::npos) {
            // No separator: keep the whole line as the label. The reader on the
            // site_info path is this forgiving too, and a file format that
            // refused what the store already holds could not export a real
            // tenant.
            link.label = line;
        } else {
            link.label = line.substr(0, bar);
            link.url = line.substr(bar + 1);
        }
        links.push_back(link);
    }
    return links;
}

std::string PackLabelledLinks(const std::vector<BundleLabelledLink>& links) {
    std::vector<std::string> lines;
    lines.reserve(links.size());
    for (const BundleLabelledLink& link : links) {
        // A label-only entry packs back WITHOUT a trailing bar, so the
        // unpack/pack pair is an exact identity rather than growing a `|`.
        lines.push_back(link.url.empty() ? link.label : link.label + "|" + link.url);
    }
    return PackLines(lines);
}

// ---- serialise --------------------------------------------------------------

Json::Value ThemeBundleToJson(const ThemeBundle& bundle) {
    // Content: registry-driven, so a slot added tomorrow appears here without
    // anyone remembering to update this function.
    Json::JsonObject content;
    for (const std::string& key : BundleContentKeys()) {
        auto it = bundle.content.find(key);
        const std::string value = it == bundle.content.end() ? std::string() : it->second;
        if (IsLabelledLinksSlot(key)) {
            Json::JsonArray links;
            for (const BundleLabelledLink& link : UnpackLabelledLinks(value)) {
                links.push_back(Json::Value(Json::JsonObject{
                    {"label", Json::Value(link.label)},
                    {"url", Json::Value(link.url)},
                }));
            }
            content[key] = Json::Value(links);
        } else if (IsLinesSlot(key)) {
            Json::JsonArray lines;
            for (const std::string& line : UnpackLines(value)) {
                lines.push_back(Json::Value(line));
            }
            content[key] = Json::Value(lines);
        } else {
            content[key] = Json::Value(value);
        }
    }

    Json::JsonObject tokens;
    for (const ThemeToken& token : SiteThemeTokens()) {
        auto it = bundle.tokens.find(std::string(token.key));
        tokens[std::string(token.key)] =
            Json::Value(it == bundle.tokens.end() ? std::string() : it->second);
    }

    Json::JsonArray sources;
    for (const BundleFontSource& source : bundle.fonts.sources) {
        Json::JsonArray preconnects;
        for (const BundleFontPreconnect& preconnect : source.preconnects) {
            preconnects.push_back(Json::Value(Json::JsonObject{
                {"url", Json::Value(preconnect.url)},
                {"crossorigin", Json::Value(preconnect.crossorigin)},
            }));
        }
        sources.push_back(Json::Value(Json::JsonObject{
            {"source_key", Json::Value(source.sourceKey)},
            {"display_name", Json::Value(source.displayName)},
            {"base_url", Json::Value(source.baseUrl)},
            {"query_suffix", Json::Value(source.querySuffix)},
            {"preconnects", Json::Value(preconnects)},
        }));
    }

    Json::JsonArray families;
    for (const BundleFontFamily& family : bundle.fonts.families) {
        Json::JsonObject object{
            {"family", Json::Value(family.family)},
            {"fallback", Json::Value(family.fallback)},
            {"source_kind", Json::Value(family.sourceKind)},
        };
        // Only emit the fields this kind actually uses — an `uploaded` family
        // carrying an empty `spec` would invite someone to fill it in.
        if (!family.sourceKey.empty()) {
            object["source_key"] = Json::Value(family.sourceKey);
        }
        if (!family.spec.empty()) {
            object["spec"] = Json::Value(family.spec);
        }
        if (!family.faces.empty()) {
            Json::JsonArray faces;
            for (const BundleFontFace& face : family.faces) {
                faces.push_back(Json::Value(Json::JsonObject{
                    {"weight", Json::Value(static_cast<int64_t>(face.weight))},
                    {"style", Json::Value(face.style)},
                    {"file", Json::Value(face.file)},
                }));
            }
            object["faces"] = Json::Value(faces);
        }
        families.push_back(Json::Value(object));
    }

    Json::JsonObject root{
        {"format", Json::Value(std::string(kThemeBundleFormat))},
        {"format_version",
         Json::Value(static_cast<int64_t>(
             bundle.formatVersion > 0 ? bundle.formatVersion
                                      : CurrentBundleFormatVersion()))},
        {"name", Json::Value(bundle.name)},
        {"description", Json::Value(bundle.description)},
        {"exported_at", Json::Value(bundle.exportedAt)},
        {"exported_from", Json::Value(Json::JsonObject{
            {"app", Json::Value(bundle.exportedFromApp)},
            {"site", Json::Value(bundle.exportedFromSite)},
            {"honuware", Json::Value(bundle.exportedFromHonuware)},
        })},
        {"theme", Json::Value(Json::JsonObject{
            {"content", Json::Value(content)},
            {"tokens", Json::Value(tokens)},
        })},
        {"fonts", Json::Value(Json::JsonObject{
            {"sources", Json::Value(sources)},
            {"families", Json::Value(families)},
        })},
    };

    for (const auto& [name, body] : bundle.appSections) {
        root[name] = body;
    }

    return Json::Value(root);
}

// ---- parse ------------------------------------------------------------------

int ReadBundleFormatVersion(const Json::Value& json) {
    return FieldInt(json, "format_version", 0);
}

std::string ThemeBundleFromJson(const Json::Value& json, ThemeBundle& out) {
    if (!json.HasChildren()) {
        return "That file is not a theme bundle.";
    }
    const std::string format = FieldString(json, "format");
    if (format != kThemeBundleFormat) {
        return "That file is not a theme bundle (format is \"" + format + "\").";
    }
    out.formatVersion = ReadBundleFormatVersion(json);
    out.name = FieldString(json, "name");
    out.description = FieldString(json, "description");
    out.exportedAt = FieldString(json, "exported_at");

    const Json::Value* exportedFrom = nullptr;
    if (json.HasChild("exported_from", &exportedFrom)) {
        out.exportedFromApp = FieldString(*exportedFrom, "app");
        out.exportedFromSite = FieldString(*exportedFrom, "site");
        out.exportedFromHonuware = FieldString(*exportedFrom, "honuware");
    }

    // Content and tokens are copied VERBATIM — every key present, known or not.
    // Deciding which keys are acceptable belongs to the import's strictness
    // check, which runs after migration has had its chance to repair them.
    const Json::Value* theme = nullptr;
    if (json.HasChild("theme", &theme)) {
        const Json::Value* content = nullptr;
        if (theme->HasChild("content", &content) && content->HasChildren()) {
            for (const auto& [key, value] : content->GetChildren()) {
                if (value.IsArray()) {
                    if (IsLabelledLinksSlot(key)) {
                        std::vector<BundleLabelledLink> links;
                        for (const Json::Value& entry : value.GetArray()) {
                            BundleLabelledLink link;
                            if (entry.HasChildren()) {
                                link.label = FieldString(entry, "label");
                                link.url = FieldString(entry, "url");
                            } else {
                                link.label = ValueAsString(entry);
                            }
                            links.push_back(link);
                        }
                        out.content[key] = PackLabelledLinks(links);
                    } else {
                        std::vector<std::string> lines;
                        for (const Json::Value& entry : value.GetArray()) {
                            lines.push_back(ValueAsString(entry));
                        }
                        out.content[key] = PackLines(lines);
                    }
                } else {
                    out.content[key] = ValueAsString(value);
                }
            }
        }
        const Json::Value* tokens = nullptr;
        if (theme->HasChild("tokens", &tokens) && tokens->HasChildren()) {
            for (const auto& [key, value] : tokens->GetChildren()) {
                out.tokens[key] = ValueAsString(value);
            }
        }
    }

    const Json::Value* fonts = nullptr;
    if (json.HasChild("fonts", &fonts)) {
        const Json::Value* sources = nullptr;
        if (fonts->HasChild("sources", &sources) && sources->IsArray()) {
            for (const Json::Value& entry : sources->GetArray()) {
                BundleFontSource source;
                source.sourceKey = FieldString(entry, "source_key");
                source.displayName = FieldString(entry, "display_name");
                source.baseUrl = FieldString(entry, "base_url");
                source.querySuffix = FieldString(entry, "query_suffix");
                const Json::Value* preconnects = nullptr;
                if (entry.HasChild("preconnects", &preconnects) &&
                    preconnects->IsArray()) {
                    for (const Json::Value& p : preconnects->GetArray()) {
                        BundleFontPreconnect preconnect;
                        preconnect.url = FieldString(p, "url");
                        preconnect.crossorigin = FieldBool(p, "crossorigin", false);
                        source.preconnects.push_back(preconnect);
                    }
                }
                out.fonts.sources.push_back(source);
            }
        }
        const Json::Value* families = nullptr;
        if (fonts->HasChild("families", &families) && families->IsArray()) {
            for (const Json::Value& entry : families->GetArray()) {
                BundleFontFamily family;
                family.family = FieldString(entry, "family");
                family.fallback = FieldString(entry, "fallback");
                family.sourceKind = FieldString(entry, "source_kind");
                family.sourceKey = FieldString(entry, "source_key");
                family.spec = FieldString(entry, "spec");
                const Json::Value* faces = nullptr;
                if (entry.HasChild("faces", &faces) && faces->IsArray()) {
                    for (const Json::Value& f : faces->GetArray()) {
                        BundleFontFace face;
                        face.weight = FieldInt(f, "weight", 400);
                        face.style = FieldString(f, "style");
                        face.file = FieldString(f, "file");
                        family.faces.push_back(face);
                    }
                }
                out.fonts.families.push_back(family);
            }
        }
    }

    // Anything at the root that is not an envelope field or a framework section
    // is an APP section, carried opaquely. This is what lets a bundle from
    // another app arrive intact and be reported as skipped rather than lost.
    static const std::set<std::string> reserved = {
        "format", "format_version", "name", "description",
        "exported_at", "exported_from", "theme", "fonts",
    };
    for (const auto& [key, value] : json.GetChildren()) {
        if (reserved.find(key) == reserved.end()) {
            out.appSections[key] = value;
        }
    }

    return {};
}

}  // namespace Branding
