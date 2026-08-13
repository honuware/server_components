#include "site_info.h"

#include <stdexcept>
#include <string>

#include "endpoint_auth_helper.h"
#include "web_app.h"
#include "business_logic/branding/site_content_slots.h"
#include "business_logic/branding/site_theme_tokens.h"
#include "business_logic/tenancy/tenant_context.h"
#include "sql_util/database_access/transaction.h"
#include "util/error_response.h"
#include "util/mail/tenant_branding.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper.h"

namespace Endpoints {

namespace {

void HandleGet(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        Json::Value result = GetSiteInfo(endpointAuthHelper);
        resp.code = 200;
        // Public + cacheable: branding changes rarely and the body carries no
        // per-user data. Matches the plan's Cache-Control (5 minutes). The site
        // header CloudFront injects makes each tenant's cache entry distinct.
        resp.set_header("Cache-Control", "public, max-age=300");
        resp.set_header("Content-Type", "application/json");
        resp.write(result.ToString());
    }
    catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/site_info")
            .methods(crow::HTTPMethod::Get)(
                [=](const crow::request& req, crow::response& resp) {
                    HandleGet(webApp, req, resp);
                });
    }
} g_setupRouting;

}  // namespace

Json::Value BuildSiteInfoResponse(
    std::string_view displayName,
    std::string_view websiteUrl,
    std::string_view logoUrl,
    const KeyValueTable& content,
    const KeyValueTable& theme,
    const Branding::SiteFontInventory& fonts) {
    Json::JsonArray preconnects;
    for (const Branding::FontPreconnect& preconnect : fonts.preconnects) {
        preconnects.push_back(Json::Value(Json::JsonObject{
            {"href", Json::Value(preconnect.href)},
            {"crossorigin", Json::Value(preconnect.crossorigin)},
        }));
    }
    Json::JsonArray stylesheets;
    for (const std::string& url : fonts.stylesheets) {
        stylesheets.push_back(Json::Value(url));
    }
    Json::JsonArray faces;
    for (const Branding::FontFaceDescriptor& face : fonts.faces) {
        faces.push_back(Json::Value(Json::JsonObject{
            {"family", Json::Value(face.family)},
            {"weight", Json::Value(static_cast<int64_t>(face.weight))},
            {"style", Json::Value(face.style)},
            {"format", Json::Value(face.format)},
            {"url", Json::Value(face.url)},
        }));
    }

    return Json::Value(Json::JsonObject{
        {"display_name", Json::Value(std::string(displayName))},
        {"website_url", Json::Value(std::string(websiteUrl))},
        {"logo_url", Json::Value(std::string(logoUrl))},
        // Json::Value's map constructor keeps every value a JSON STRING.
        // SqlUtil::KeyValueTableToJson would be the reflex here, but it promotes
        // integer-looking strings to numbers — turning a browser title of "2026"
        // into something the SPA has to re-stringify. Branding copy is text.
        {"content", Json::Value(content)},
        {"theme", Json::Value(theme)},
        // Tenant Theming Phase 4B — what the boot initializer needs to LOAD the
        // tenant's fonts, as opposed to `theme`'s --font-* roles which say which
        // family does which job.
        {"fonts", Json::Value(Json::JsonObject{
            {"preconnects", Json::Value(preconnects)},
            {"stylesheets", Json::Value(stylesheets)},
            {"faces", Json::Value(faces)},
        })},
    });
}

Json::Value GetSiteInfo(EndpointAuthHelper& endpointAuthHelper) {
    std::string displayName;
    std::string websiteUrl;
    std::string logoUrl;
    KeyValueTable content;
    KeyValueTable theme;
    Branding::SiteFontInventory fonts;
    endpointAuthHelper.GetTransactionProvider()->RunInTransaction(
        [&](Transaction& transaction) {
            Secrets::SecretsHelperPtr secrets =
                endpointAuthHelper.GetSecretsHelper();
            Mail::TenantBranding branding =
                Mail::LoadTenantBranding(*secrets, transaction);
            displayName = branding.studioName;
            websiteUrl = branding.websiteUrl;
            logoUrl = secrets->LookupSecret(transaction, Secrets::kSiteLogoUrl);
            content = Branding::LoadSiteContent(*secrets, transaction);
            theme = Branding::LoadSiteTheme(*secrets, transaction);
            fonts = Branding::LoadSiteFontInventory(
                endpointAuthHelper.GetDatabaseHelper(), transaction,
                "/api/site_font_face/");

            // D13: a --font-* role names a FAMILY; the stack it resolves to is
            // composed from that family's row, so the fallback is defined in
            // exactly one place. A role naming a family with no row keeps the
            // stylesheet's default rather than emitting a bare family name.
            for (const char* role : {"--font-body", "--font-heading",
                                     "--font-display"}) {
                auto it = theme.find(role);
                if (it == theme.end()) {
                    continue;
                }
                std::string stack = Branding::LookupFontStack(
                    endpointAuthHelper.GetDatabaseHelper(), transaction,
                    it->second);
                if (stack.empty()) {
                    theme.erase(it);
                } else {
                    it->second = stack;
                }
            }
        });
    // Fall back to the tenants-row display name when the branding secret is blank
    // (e.g. a freshly provisioned tenant that hasn't set kMailSenderName yet).
    if (displayName.empty()) {
        displayName = endpointAuthHelper.GetTenantContext().displayName;
    }
    return BuildSiteInfoResponse(
        displayName, websiteUrl, logoUrl, content, theme, fonts);
}

}  // namespace Endpoints
