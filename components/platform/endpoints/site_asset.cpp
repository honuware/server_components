#include "site_asset.h"

#include "business_logic/branding/theme_bundle_assets.h"
#include "db_schema/site_assets.h"
#include "endpoints/endpoint_auth_helper.h"
#include "sql_util/table_helpers/site_assets.h"
#include "util/error_response.h"

namespace Endpoints {
namespace {

// The MIME type for a stored image type. Anything we do not recognise is served
// as octet-stream rather than guessed at: with nosniff set, an unknown type is
// downloaded instead of rendered, which is the safe end of the trade.
std::string ContentTypeForImage(const std::string& type) {
    if (type == "png") return "image/png";
    if (type == "jpeg") return "image/jpeg";
    if (type == "gif") return "image/gif";
    if (type == "webp") return "image/webp";
    if (type == "svg") return "image/svg+xml";
    return "application/octet-stream";
}

void HandleGet(
    WebApp* webApp,
    const crow::request& req,
    crow::response& resp,
    const std::string& name) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        GetSiteAsset(endpointAuthHelper, req, resp, name);
    }
    catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/site_asset/<string>")
            .methods(crow::HTTPMethod::Get)(
                [=](const crow::request& req, crow::response& resp,
                    const std::string& name) {
                    HandleGet(webApp, req, resp, name);
                });
    }
} g_setupRouting;

}  // namespace

void GetSiteAsset(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp,
    const std::string& name) {

    // Checked before the name ever reaches a query. The same rule the importer
    // applies, so a name that could not have been imported cannot be requested.
    if (!Branding::IsValidBundleAssetName(name)) {
        resp = ErrorResponse::NotFound("No such image");
        return;
    }

    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::InternalError("Database unavailable");
        return;
    }

    tp->RunInTransaction([&](Transaction& transaction) {
        TableHelpers::SiteAssets assets(endpointAuthHelper.GetDatabaseHelper());
        KeyValueTable row = assets.GetAssetByName(transaction, name);
        if (row.empty()) {
            resp = ErrorResponse::NotFound("No such image");
            return;
        }
        const std::string bytes = assets.GetAssetBytes(transaction, name);
        if (bytes.empty()) {
            resp = ErrorResponse::NotFound("No such image");
            return;
        }
        auto type = row.find(std::string(DbSchema::kSiteAssetType));
        resp.code = 200;
        resp.set_header("Content-Type", ContentTypeForImage(
            type == row.end() ? std::string() : type->second));
        // Bytes an admin supplied, served from our origin: never let a browser
        // decide for itself what they are.
        resp.set_header("X-Content-Type-Options", "nosniff");
        // Addressed by NAME, so importing a theme replaces this in place — a
        // held copy would be the previous theme's logo.
        resp.set_header("Cache-Control", "no-cache");
        resp.write(bytes);
    });
}

}  // namespace Endpoints
