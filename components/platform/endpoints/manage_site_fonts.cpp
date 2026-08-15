#include "manage_site_fonts.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "business_logic/auth/session.h"
#include "business_logic/branding/site_font_inventory.h"
#include "business_logic/branding/site_value_validation.h"
#include "db_schema/site_fonts.h"
#include "endpoints/endpoint_auth_helper.h"
#include "sql_util/table_helpers/site_fonts.h"
#include "util/error_response.h"
#include "util/json_value.h"
#include "util/types.h"

namespace Endpoints {
namespace {

std::string ValueOr(const KeyValueTable& row, std::string_view column) {
    auto it = row.find(std::string(column));
    return it == row.end() ? std::string() : it->second;
}

// Admin gate shared by all four handlers.
bool RequireAdmin(
    EndpointAuthHelper& endpointAuthHelper,
    Transaction& transaction,
    crow::response& resp) {
    Auth::Session& session = endpointAuthHelper.GetSession();
    if (!session.IsLoggedIn()) {
        resp = ErrorResponse::NotAuthenticated("Login required");
        return false;
    }
    if (!session.IsAdmin(transaction)) {
        resp = ErrorResponse::NotAuthorized("Admin required");
        return false;
    }
    return true;
}

std::string FieldString(const Json::Value& object, const char* name) {
    const Json::Value* field = nullptr;
    if (!object.HasChild(name, &field)) {
        return {};
    }
    return field->Get<std::string>();
}

int64_t FieldInt(const Json::Value& object, const char* name) {
    const Json::Value* field = nullptr;
    if (!object.HasChild(name, &field)) {
        return 0;
    }
    // The editor sends ids as numbers and new rows as 0/absent.
    if (field->Is<int64_t>()) {
        return field->Get<int64_t>();
    }
    std::string text = field->Get<std::string>();
    return text.empty() ? 0 : std::atoll(text.c_str());
}

void HandleGet(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper helper(*webApp, req, resp);
        helper.Initialize();
        Json::Value result = GetManageSiteFonts(helper, req, resp);
        if (resp.code == 200) {
            resp.set_header("Content-Type", "application/json");
            resp.write(result.ToString());
        }
    } catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

void HandlePut(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper helper(*webApp, req, resp);
        helper.Initialize();
        Json::Value result = PutManageSiteFonts(helper, req, resp);
        if (resp.code == 200) {
            resp.set_header("Content-Type", "application/json");
            resp.write(result.ToString());
        }
    } catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

void HandlePostFace(
    WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper helper(*webApp, req, resp);
        helper.Initialize();
        Json::Value result = PostManageSiteFontFace(helper, req, resp);
        if (resp.code == 200) {
            resp.set_header("Content-Type", "application/json");
            resp.write(result.ToString());
        }
    } catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

void HandleDeleteFace(
    WebApp* webApp, const crow::request& req, crow::response& resp,
    int64_t faceId) {
    try {
        EndpointAuthHelper helper(*webApp, req, resp);
        helper.Initialize();
        Json::Value result =
            DeleteManageSiteFontFace(helper, req, resp, faceId);
        if (resp.code == 200) {
            resp.set_header("Content-Type", "application/json");
            resp.write(result.ToString());
        }
    } catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/manage/site_fonts")
            .methods(crow::HTTPMethod::Get)(
                [=](const crow::request& req, crow::response& resp) {
                    HandleGet(webApp, req, resp);
                });
        CROW_ROUTE(webApp->GetApp(), "/api/manage/site_fonts")
            .methods(crow::HTTPMethod::Put)(
                [=](const crow::request& req, crow::response& resp) {
                    HandlePut(webApp, req, resp);
                });
        // Path segments rather than a JSON envelope, because the BODY is the
        // raw font file — the same shape upload_photo uses.
        CROW_ROUTE(webApp->GetApp(),
                   "/api/manage/site_font_face/<int>/<int>/<string>")
            .methods(crow::HTTPMethod::Post)(
                [=](const crow::request& req, crow::response& resp,
                    int64_t fontId, int64_t weight, const std::string& style) {
                    crow::request copy = req;
                    // Stash the path segments where the exported function can
                    // read them without re-parsing the URL.
                    copy.add_header("X-Font-Id", std::to_string(fontId));
                    copy.add_header("X-Font-Weight", std::to_string(weight));
                    copy.add_header("X-Font-Style", style);
                    HandlePostFace(webApp, copy, resp);
                });
        CROW_ROUTE(webApp->GetApp(), "/api/manage/site_font_face/<int>")
            .methods(crow::HTTPMethod::Delete)(
                [=](const crow::request& req, crow::response& resp,
                    int64_t faceId) {
                    HandleDeleteFace(webApp, req, resp, faceId);
                });
    }
} g_setupRouting;

}  // namespace

Json::Value GetManageSiteFonts(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp) {

    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::InternalError("Database unavailable");
        return {};
    }

    Json::Value result;
    tp->RunInTransaction([&](Transaction& transaction) {
        if (!RequireAdmin(endpointAuthHelper, transaction, resp)) {
            return;
        }
        TableHelpers::SiteFonts fonts(endpointAuthHelper.GetDatabaseHelper());

        Json::JsonArray sources;
        for (const KeyValueTable& row : fonts.GetAllSources(transaction)) {
            sources.push_back(Json::Value(Json::JsonObject{
                {"id", Json::Value(ValueOr(row, DbSchema::kSiteFontSourceId))},
                {"source_key", Json::Value(ValueOr(row, DbSchema::kSiteFontSourceKey))},
                {"display_name", Json::Value(ValueOr(row, DbSchema::kSiteFontSourceDisplayName))},
                {"base_url", Json::Value(ValueOr(row, DbSchema::kSiteFontSourceBaseUrl))},
                {"query_suffix", Json::Value(ValueOr(row, DbSchema::kSiteFontSourceQuerySuffix))},
                {"preconnect_lines", Json::Value(ValueOr(row, DbSchema::kSiteFontSourcePreconnectLines))},
            }));
        }

        Json::JsonArray families;
        for (const KeyValueTable& row : fonts.GetAllFonts(transaction)) {
            std::string fontId = ValueOr(row, DbSchema::kSiteFontId);
            Json::JsonArray faces;
            for (const KeyValueTable& face :
                 fonts.GetFacesForFont(transaction, std::atoll(fontId.c_str()))) {
                faces.push_back(Json::Value(Json::JsonObject{
                    {"id", Json::Value(ValueOr(face, DbSchema::kSiteFontFaceId))},
                    {"weight", Json::Value(ValueOr(face, DbSchema::kSiteFontFaceWeight))},
                    {"style", Json::Value(ValueOr(face, DbSchema::kSiteFontFaceStyle))},
                    {"format", Json::Value(ValueOr(face, DbSchema::kSiteFontFaceFormat))},
                }));
            }
            families.push_back(Json::Value(Json::JsonObject{
                {"id", Json::Value(fontId)},
                {"family", Json::Value(ValueOr(row, DbSchema::kSiteFontFamily))},
                {"fallback", Json::Value(ValueOr(row, DbSchema::kSiteFontFallback))},
                {"source_kind", Json::Value(ValueOr(row, DbSchema::kSiteFontSourceKind))},
                {"font_source_id", Json::Value(ValueOr(row, DbSchema::kSiteFontFontSourceId))},
                {"spec", Json::Value(ValueOr(row, DbSchema::kSiteFontSpec))},
                {"ordinal", Json::Value(ValueOr(row, DbSchema::kSiteFontOrdinal))},
                {"faces", Json::Value(faces)},
            }));
        }

        result = Json::Value(Json::JsonObject{
            {"sources", Json::Value(sources)},
            {"families", Json::Value(families)},
            {"max_face_bytes",
             Json::Value(static_cast<int64_t>(Branding::kMaxFontFaceBytes))},
        });
        resp.code = 200;
    });

    return result;
}

Json::Value PutManageSiteFonts(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp) {

    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::InternalError("Database unavailable");
        return {};
    }

    Json::Value result;
    tp->RunInTransaction([&](Transaction& transaction) {
        if (!RequireAdmin(endpointAuthHelper, transaction, resp)) {
            return;
        }
        Json::Value body = Json::Value::FromText(req.body);
        if (!body.HasChildren()) {
            resp = ErrorResponse::BadRequest("No message body.");
            return;
        }

        TableHelpers::SiteFonts fonts(endpointAuthHelper.GetDatabaseHelper());

        // ---- validate EVERYTHING before writing anything ----
        const Json::Value* sources = nullptr;
        const Json::Value* families = nullptr;
        body.HasChild("sources", &sources);
        body.HasChild("families", &families);

        std::set<std::string> sourceKeys;
        if (sources && sources->IsArray()) {
            for (const auto& source : sources->GetArray()) {
                std::string key = FieldString(source, "source_key");
                std::string baseUrl = FieldString(source, "base_url");
                if (key.empty()) {
                    resp = ErrorResponse::ValidationError(
                        "Every font source needs a key.");
                    return;
                }
                if (!sourceKeys.insert(key).second) {
                    resp = ErrorResponse::ValidationError(
                        "Two font sources share the key '" + key + "'.");
                    return;
                }
                if (!Branding::IsValidFontSourceUrl(baseUrl)) {
                    resp = ErrorResponse::ValidationError(
                        key + ": the address must be an https:// URL.");
                    return;
                }
                std::string suffix = FieldString(source, "query_suffix");
                if (!suffix.empty() && !Branding::IsValidFontSpec(suffix)) {
                    resp = ErrorResponse::ValidationError(
                        key + ": that URL suffix is not a valid query fragment.");
                    return;
                }
                for (const Branding::FontPreconnect& preconnect :
                     Branding::ParsePreconnectLines(
                         FieldString(source, "preconnect_lines"))) {
                    (void)preconnect;  // ParsePreconnectLines drops invalid rows
                }
            }
        }

        std::set<std::string> familyNames;
        if (families && families->IsArray()) {
            for (const auto& family : families->GetArray()) {
                std::string name = FieldString(family, "family");
                std::string fallback = FieldString(family, "fallback");
                std::string kind = FieldString(family, "source_kind");
                if (!Branding::IsValidFontFamilyName(name)) {
                    resp = ErrorResponse::ValidationError(
                        "'" + name + "' is not a usable font family name.");
                    return;
                }
                if (!familyNames.insert(name).second) {
                    resp = ErrorResponse::ValidationError(
                        "Two families are both called '" + name + "'.");
                    return;
                }
                // D13: the fallback is REQUIRED and never assumed.
                if (fallback.empty() ||
                    !Branding::IsValidFontFamilyList(fallback)) {
                    resp = ErrorResponse::ValidationError(
                        name + ": needs a fallback (for example sans-serif).");
                    return;
                }
                if (kind != DbSchema::kSiteFontSourceKindCdn &&
                    kind != DbSchema::kSiteFontSourceKindUploaded &&
                    kind != DbSchema::kSiteFontSourceKindSystem) {
                    resp = ErrorResponse::ValidationError(
                        name + ": unknown font source kind '" + kind + "'.");
                    return;
                }
                if (kind == DbSchema::kSiteFontSourceKindCdn) {
                    std::string spec = FieldString(family, "spec");
                    if (!Branding::IsValidFontSpec(spec)) {
                        resp = ErrorResponse::ValidationError(
                            name + ": that font specification is not valid.");
                        return;
                    }
                    if (FieldString(family, "source_key").empty()) {
                        resp = ErrorResponse::ValidationError(
                            name + ": a downloaded font needs a source.");
                        return;
                    }
                }
            }
        }

        // ---- reconcile ----
        //
        // Matched by KEY (sources) and FAMILY NAME (families), updating in
        // place and deleting only what the payload dropped.
        //
        // This must not be a delete-everything-then-re-add: a family's uploaded
        // font FILES hang off its row id, so recreating the row would destroy
        // every face the studio uploaded — on a save that merely renamed a
        // different family. An admin cannot be expected to re-upload their
        // brand typeface after editing an unrelated row.

        std::map<std::string, int64_t> sourceIdByKey;
        std::set<std::string> keptSourceKeys;
        if (sources && sources->IsArray()) {
            for (const auto& source : sources->GetArray()) {
                std::string key = FieldString(source, "source_key");
                std::string displayName = FieldString(source, "display_name");
                std::string baseUrl = FieldString(source, "base_url");
                std::string suffix = FieldString(source, "query_suffix");
                std::string preconnects = FieldString(source, "preconnect_lines");
                KeyValueTable existing = fonts.GetSourceByKey(transaction, key);
                if (existing.empty()) {
                    sourceIdByKey[key] = fonts.AddSource(
                        transaction, key, displayName, baseUrl, suffix, preconnects);
                } else {
                    int64_t id = std::atoll(
                        ValueOr(existing, DbSchema::kSiteFontSourceId).c_str());
                    fonts.UpdateSource(
                        transaction, id, key, displayName, baseUrl, suffix, preconnects);
                    sourceIdByKey[key] = id;
                }
                keptSourceKeys.insert(key);
            }
        }

        int ordinal = 10;
        std::set<std::string> keptFamilies;
        if (families && families->IsArray()) {
            for (const auto& family : families->GetArray()) {
                std::string name = FieldString(family, "family");
                std::string fallback = FieldString(family, "fallback");
                std::string kind = FieldString(family, "source_kind");
                int64_t sourceId = 0;
                std::string spec;
                if (kind == DbSchema::kSiteFontSourceKindCdn) {
                    auto it = sourceIdByKey.find(FieldString(family, "source_key"));
                    sourceId = it == sourceIdByKey.end() ? 0 : it->second;
                    spec = FieldString(family, "spec");
                }
                KeyValueTable existing = fonts.GetFontByFamily(transaction, name);
                if (existing.empty()) {
                    fonts.AddFont(
                        transaction, name, fallback, kind, sourceId, spec, ordinal);
                } else {
                    fonts.UpdateFont(
                        transaction,
                        std::atoll(ValueOr(existing, DbSchema::kSiteFontId).c_str()),
                        name, fallback, kind, sourceId, spec, ordinal);
                }
                keptFamilies.insert(name);
                ordinal += 10;
            }
        }

        // Now drop what the payload no longer lists. Families first (a removed
        // family's faces must go before it does), then sources — a source can
        // only be removed once nothing references it.
        for (const KeyValueTable& row : fonts.GetAllFonts(transaction)) {
            if (keptFamilies.count(ValueOr(row, DbSchema::kSiteFontFamily))) {
                continue;
            }
            int64_t id = std::atoll(ValueOr(row, DbSchema::kSiteFontId).c_str());
            for (const KeyValueTable& face : fonts.GetFacesForFont(transaction, id)) {
                fonts.DeleteFace(transaction,
                    std::atoll(ValueOr(face, DbSchema::kSiteFontFaceId).c_str()));
            }
            fonts.DeleteFont(transaction, id);
        }
        for (const KeyValueTable& row : fonts.GetAllSources(transaction)) {
            if (keptSourceKeys.count(ValueOr(row, DbSchema::kSiteFontSourceKey))) {
                continue;
            }
            fonts.DeleteSource(transaction,
                std::atoll(ValueOr(row, DbSchema::kSiteFontSourceId).c_str()));
        }

        result = Json::Value(Json::JsonObject{
            {"sources", Json::Value(static_cast<int64_t>(keptSourceKeys.size()))},
            {"families", Json::Value(static_cast<int64_t>(keptFamilies.size()))},
        });
        resp.code = 200;
    });

    return result;
}

Json::Value PostManageSiteFontFace(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp) {

    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::InternalError("Database unavailable");
        return {};
    }

    Json::Value result;
    tp->RunInTransaction([&](Transaction& transaction) {
        if (!RequireAdmin(endpointAuthHelper, transaction, resp)) {
            return;
        }

        int64_t fontId = std::atoll(req.get_header_value("X-Font-Id").c_str());
        int weight = std::atoi(req.get_header_value("X-Font-Weight").c_str());
        std::string style = req.get_header_value("X-Font-Style");
        if (style != "italic") {
            style = "normal";
        }
        if (weight < 1 || weight > 1000) {
            resp = ErrorResponse::ValidationError(
                "A font weight must be between 1 and 1000.");
            return;
        }

        if (req.body.empty()) {
            resp = ErrorResponse::BadRequest("No font file in the request.");
            return;
        }
        if (req.body.size() > Branding::kMaxFontFaceBytes) {
            resp = ErrorResponse::ValidationError(
                "That font file is too large.");
            return;
        }
        // D14: the FORMAT is decided by the magic bytes, never the filename —
        // this is what stops something that is not a font being stored and then
        // served back from our own origin.
        std::string format = Branding::FontFormatFromMagicBytes(req.body);
        if (format.empty()) {
            resp = ErrorResponse::ValidationError(
                "That file is not a WOFF2, WOFF, TTF or OTF font.");
            return;
        }

        TableHelpers::SiteFonts fonts(endpointAuthHelper.GetDatabaseHelper());
        if (fonts.GetFont(transaction, fontId).empty()) {
            resp = ErrorResponse::NotFound("Font family not found");
            return;
        }

        int64_t faceId = fonts.AddFace(
            transaction, fontId, weight, style, format, req.body);

        result = Json::Value(Json::JsonObject{
            {"id", Json::Value(faceId)},
            {"format", Json::Value(format)},
            {"weight", Json::Value(static_cast<int64_t>(weight))},
            {"style", Json::Value(style)},
        });
        resp.code = 200;
    });

    return result;
}

Json::Value DeleteManageSiteFontFace(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp,
    int64_t faceId) {

    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::InternalError("Database unavailable");
        return {};
    }

    Json::Value result;
    tp->RunInTransaction([&](Transaction& transaction) {
        if (!RequireAdmin(endpointAuthHelper, transaction, resp)) {
            return;
        }
        TableHelpers::SiteFonts fonts(endpointAuthHelper.GetDatabaseHelper());
        if (fonts.GetFace(transaction, faceId).empty()) {
            resp = ErrorResponse::NotFound("Font face not found");
            return;
        }
        fonts.DeleteFace(transaction, faceId);
        result = Json::Value(Json::JsonObject{{"deleted", Json::Value(true)}});
        resp.code = 200;
    });

    return result;
}

}  // namespace Endpoints
