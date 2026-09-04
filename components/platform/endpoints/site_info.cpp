#include "site_info.h"

#include <stdexcept>
#include <string>

#include "endpoint_auth_helper.h"
#include "web_app.h"
#include "business_logic/branding/site_content_slots.h"
#include "business_logic/branding/site_info_app_blocks.h"
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
        // Public but REVALIDATED, not held for five minutes.
        //
        // This response decides how the whole site looks, so a stale copy is not
        // a minor staleness — it is the tenant's branding silently not applying.
        // With `max-age=300` an admin could save a colour, reload, see no change,
        // and reasonably conclude the feature was broken (reported 8/14/2026;
        // the value was in the database and in this payload the whole time).
        //
        // `no-cache` still allows storing — it requires revalidation before
        // reuse — so a shared cache keeps working and the cost of being correct
        // is one conditional request per boot on a ~1.5 KB body. Branding is
        // read once per page load, not per request; this is not a hot path.
        resp.set_header("Cache-Control", "no-cache");
        // This body is tenant-specific and control mode picks the tenant from a
        // request header, which is NOT part of a shared cache's key by default.
        // Without this, one studio's branding could be served to another.
        resp.set_header("Vary", "X-Honuware-Site");
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
    const Branding::SiteFontInventory& fonts,
    const Json::JsonObject& appBlocks) {
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
        // Whatever the app registered (Branding::RegisterSiteInfoBlock).
        // Always present, even when empty — a client should not have to tell
        // "this app contributes nothing" apart from "this server predates the
        // seam".
        {"app", Json::Value(appBlocks)},
    });
}

Json::Value GetSiteInfo(EndpointAuthHelper& endpointAuthHelper) {
    std::string displayName;
    std::string websiteUrl;
    std::string logoUrl;
    KeyValueTable content;
    KeyValueTable theme;
    Branding::SiteFontInventory fonts;
    Json::JsonObject appBlocks;
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

            // App-contributed blocks, in the same transaction so a block reads
            // a consistent snapshot with the branding above it.
            for (const Branding::SiteInfoBlock& block :
                     Branding::SiteInfoBlocks()) {
                appBlocks[block.name] = block.build(
                    transaction, endpointAuthHelper.GetDatabaseHelper());
            }
        });
    // Fall back to the tenants-row display name when the branding secret is blank
    // (e.g. a freshly provisioned tenant that hasn't set kMailSenderName yet).
    if (displayName.empty()) {
        displayName = endpointAuthHelper.GetTenantContext().displayName;
    }
    return BuildSiteInfoResponse(
        displayName, websiteUrl, logoUrl, content, theme, fonts, appBlocks);
}

}  // namespace Endpoints
