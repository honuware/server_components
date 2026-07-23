#include "site_info.h"

#include <stdexcept>
#include <string>

#include "endpoint_auth_helper.h"
#include "web_app.h"
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
    std::string_view logoUrl) {
    return Json::Value(Json::JsonObject{
        {"display_name", Json::Value(std::string(displayName))},
        {"website_url", Json::Value(std::string(websiteUrl))},
        {"logo_url", Json::Value(std::string(logoUrl))},
    });
}

Json::Value GetSiteInfo(EndpointAuthHelper& endpointAuthHelper) {
    std::string displayName;
    std::string websiteUrl;
    std::string logoUrl;
    endpointAuthHelper.GetTransactionProvider()->RunInTransaction(
        [&](Transaction& transaction) {
            Secrets::SecretsHelperPtr secrets =
                endpointAuthHelper.GetSecretsHelper();
            Mail::TenantBranding branding =
                Mail::LoadTenantBranding(*secrets, transaction);
            displayName = branding.studioName;
            websiteUrl = branding.websiteUrl;
            logoUrl = secrets->LookupSecret(transaction, Secrets::kSiteLogoUrl);
        });
    // Fall back to the tenants-row display name when the branding secret is blank
    // (e.g. a freshly provisioned tenant that hasn't set kMailSenderName yet).
    if (displayName.empty()) {
        displayName = endpointAuthHelper.GetTenantContext().displayName;
    }
    return BuildSiteInfoResponse(displayName, websiteUrl, logoUrl);
}

}  // namespace Endpoints
