#include "manage_site_theme.h"

#include <string>
#include <vector>

#include "business_logic/auth/session.h"
#include "business_logic/branding/site_content_slots.h"
#include "business_logic/branding/site_font_inventory.h"
#include "business_logic/branding/site_theme_tokens.h"
#include "db_schema/site_fonts.h"
#include "endpoints/endpoint_auth_helper.h"
#include "sql_util/table_helpers/site_fonts.h"
#include "util/error_response.h"
#include "util/json_value.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper.h"
#include "util/types.h"

namespace Endpoints {
namespace {

// The slot types, as strings the editor can switch its control on.
std::string SlotTypeName(Branding::SlotType type) {
    switch (type) {
        case Branding::SlotType::Line: return "line";
        case Branding::SlotType::Lines: return "lines";
        case Branding::SlotType::Markdown: return "markdown";
        case Branding::SlotType::Url: return "url";
        case Branding::SlotType::Color: return "color";
    }
    return "line";
}

std::string TokenTypeName(Branding::ThemeTokenType type) {
    switch (type) {
        case Branding::ThemeTokenType::Color: return "color";
        case Branding::ThemeTokenType::Length: return "length";
        case Branding::ThemeTokenType::FontFamily: return "font";
        case Branding::ThemeTokenType::Weight: return "weight";
    }
    return "color";
}

// The editor's section for this token (Phase 6B / D16).
std::string TokenGroupName(Branding::ThemeTokenGroup group) {
    switch (group) {
        case Branding::ThemeTokenGroup::Brand: return "brand";
        case Branding::ThemeTokenGroup::Surface: return "surface";
        case Branding::ThemeTokenGroup::Status: return "status";
        case Branding::ThemeTokenGroup::Shell: return "shell";
        case Branding::ThemeTokenGroup::Palette: return "palette";
        case Branding::ThemeTokenGroup::Radius: return "radius";
        case Branding::ThemeTokenGroup::TypeScale: return "type_scale";
        case Branding::ThemeTokenGroup::FontRole: return "font_role";
    }
    return "brand";
}

void HandleGet(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        Json::Value result = GetManageSiteTheme(endpointAuthHelper, req, resp);
        if (resp.code == 200) {
            resp.set_header("Content-Type", "application/json");
            resp.write(result.ToString());
        }
    }
    catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

void HandlePut(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        Json::Value result = PutManageSiteTheme(endpointAuthHelper, req, resp);
        if (resp.code == 200) {
            resp.set_header("Content-Type", "application/json");
            resp.write(result.ToString());
        }
    }
    catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/manage/site_theme")
            .methods(crow::HTTPMethod::Get)(
                [=](const crow::request& req, crow::response& resp) {
                    HandleGet(webApp, req, resp);
                });
        CROW_ROUTE(webApp->GetApp(), "/api/manage/site_theme")
            .methods(crow::HTTPMethod::Put)(
                [=](const crow::request& req, crow::response& resp) {
                    HandlePut(webApp, req, resp);
                });
    }
} g_setupRouting;

}  // namespace

Json::Value GetManageSiteTheme(
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
        Auth::Session& session = endpointAuthHelper.GetSession();
        if (!session.IsLoggedIn()) {
            resp = ErrorResponse::NotAuthenticated("Login required");
            return;
        }
        if (!session.IsAdmin(transaction)) {
            resp = ErrorResponse::NotAuthorized("Admin required");
            return;
        }

        Secrets::SecretsHelperPtr secrets = endpointAuthHelper.GetSecretsHelper();

        // Content slots. `value` is what is stored (raw, NOT normalized — the
        // editor must show what a studio actually typed, even when site_info
        // would drop it); `is_set` is what distinguishes "cleared" from "never
        // touched".
        Json::JsonArray content;
        for (const Branding::ContentSlot& slot : Branding::SiteContentSlots()) {
            std::string stored = secrets->LookupSecret(transaction, slot.key);
            content.push_back(Json::Value(Json::JsonObject{
                {"key", Json::Value(std::string(slot.key))},
                {"type", Json::Value(SlotTypeName(slot.type))},
                {"value", Json::Value(stored)},
                {"is_set", Json::Value(!stored.empty())},
            }));
        }

        // Theme tokens. An unset token has no row at all — the SPA's stylesheet
        // holds the default — so `is_set` is the whole story here.
        Json::JsonArray theme;
        for (const Branding::ThemeToken& token : Branding::SiteThemeTokens()) {
            std::string stored = secrets->LookupSecret(transaction, token.key);
            theme.push_back(Json::Value(Json::JsonObject{
                {"key", Json::Value(std::string(token.key))},
                {"css_variable", Json::Value(std::string(token.cssVariable))},
                {"type", Json::Value(TokenTypeName(token.type))},
                {"group", Json::Value(TokenGroupName(token.group))},
                // The copy lives with the registry so it cannot drift from the
                // token it describes.
                {"description", Json::Value(std::string(token.description))},
                {"value", Json::Value(stored)},
                {"is_set", Json::Value(!stored.empty())},
            }));
        }

        // The tenant's font families, read-only — enough for the role dropdowns
        // to offer real options. Editing the inventory is the font manager's job.
        Json::JsonArray families;
        TableHelpers::SiteFonts fonts(endpointAuthHelper.GetDatabaseHelper());
        for (const KeyValueTable& row : fonts.GetActiveFonts(transaction)) {
            auto family = row.find(std::string(DbSchema::kSiteFontFamily));
            auto fallback = row.find(std::string(DbSchema::kSiteFontFallback));
            auto kind = row.find(std::string(DbSchema::kSiteFontSourceKind));
            if (family == row.end()) {
                continue;
            }
            families.push_back(Json::Value(Json::JsonObject{
                {"family", Json::Value(family->second)},
                {"fallback", Json::Value(
                    fallback == row.end() ? std::string() : fallback->second)},
                {"source_kind", Json::Value(
                    kind == row.end() ? std::string() : kind->second)},
            }));
        }

        result = Json::Value(Json::JsonObject{
            {"content", Json::Value(content)},
            {"theme", Json::Value(theme)},
            {"font_families", Json::Value(families)},
        });
        resp.code = 200;
    });

    return result;
}

Json::Value PutManageSiteTheme(
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
        Auth::Session& session = endpointAuthHelper.GetSession();
        if (!session.IsLoggedIn()) {
            resp = ErrorResponse::NotAuthenticated("Login required");
            return;
        }
        if (!session.IsAdmin(transaction)) {
            resp = ErrorResponse::NotAuthorized("Admin required");
            return;
        }

        Json::Value body = Json::Value::FromText(req.body);
        if (!body.HasChildren()) {
            resp = ErrorResponse::BadRequest("No message body.");
            return;
        }

        // Gather every requested write and validate the WHOLE set before
        // touching the store: a half-applied theme is worse than a refused one.
        std::vector<std::pair<std::string, std::string>> writes;

        const Json::Value* content = nullptr;
        if (body.HasChild("content", &content) && content->HasChildren()) {
            for (const Branding::ContentSlot& slot : Branding::SiteContentSlots()) {
                const Json::Value* field = nullptr;
                if (!content->HasChild(slot.key, &field)) {
                    continue;
                }
                std::string value =
                    Branding::NormalizeLineEndings(field->Get<std::string>());
                std::string reason = Branding::ValidateSlotValue(slot.type, value);
                if (!reason.empty()) {
                    resp = ErrorResponse::ValidationError(
                        std::string(slot.key) + ": " + reason);
                    return;
                }
                writes.emplace_back(std::string(slot.key), value);
            }
        }

        const Json::Value* theme = nullptr;
        if (body.HasChild("theme", &theme) && theme->HasChildren()) {
            for (const Branding::ThemeToken& token : Branding::SiteThemeTokens()) {
                const Json::Value* field = nullptr;
                if (!theme->HasChild(token.key, &field)) {
                    continue;
                }
                std::string value = field->Get<std::string>();
                // Empty CLEARS a token — that is how "reset to default" is
                // expressed, and it is why empty is not run through the
                // value validator (which rightly refuses it).
                if (!value.empty() &&
                    !Branding::IsValidThemeTokenValue(token.type, value)) {
                    resp = ErrorResponse::ValidationError(
                        std::string(token.key) + ": not a valid " +
                        TokenTypeName(token.type) + " value.");
                    return;
                }
                writes.emplace_back(std::string(token.key), value);
            }
        }

        if (writes.empty()) {
            resp = ErrorResponse::BadRequest(
                "No recognised branding fields in the request.");
            return;
        }

        Secrets::SecretsHelperPtr secrets = endpointAuthHelper.GetSecretsHelper();
        for (const auto& [key, value] : writes) {
            secrets->AddSecret(transaction, key, value);
        }

        result = Json::Value(Json::JsonObject{
            {"saved", Json::Value(static_cast<int64_t>(writes.size()))},
        });
        resp.code = 200;
    });

    return result;
}

}  // namespace Endpoints
