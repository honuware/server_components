#include "get_photo.h"

#include "endpoints/endpoint_auth_helper.h"
#include "business_logic/auth/session.h"
#include "business_logic/images/image_helper.h"
#include "util/error_response.h"

namespace Endpoints {
namespace {

void HandleGet(
    WebApp* webApp,
    const crow::request& req,
    crow::response& resp,
    const std::string& tableName,
    int64_t tableItemId) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        GetPhoto(endpointAuthHelper, req, resp, tableName, tableItemId);
    }
    catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/get_photo/<string>/<int>")
            .methods(crow::HTTPMethod::GET)(
                [=](const crow::request& req, crow::response& resp,
                    const std::string& tableName, int64_t tableItemId) {
                    HandleGet(webApp, req, resp, tableName, tableItemId);
                });
    }
} g_setupRouting;

}  // namespace

void GetPhoto(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp,
    std::string_view tableName,
    int64_t tableItemId) {

    // Phase 3.7 of the security review: previously required only
    // IsLoggedIn, which meant any authenticated customer could fetch
    // any image keyed by (table, item_id). Now we additionally require
    // that the active user has read access to `tableName` via the
    // standard generic-CRUD allow-list — admins see all admin tables,
    // permission holders see their permission's tables, anonymous
    // users see nothing through this endpoint (use home_page_photos
    // for the public path).
    Auth::Session& session = endpointAuthHelper.GetSession();
    if (!session.IsLoggedIn()) {
        resp = ErrorResponse::NotAuthenticated("Login required");
        return;
    }

    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::InternalError("Database unavailable");
        return;
    }

    tp->RunInTransaction([&](Transaction& transaction) {
        if (!endpointAuthHelper.IsTableAllowed(transaction, tableName)) {
            resp = ErrorResponse::ValidationError(
                "Table(" + std::string(tableName) + ") is not an allowed table.");
            return;
        }

        Images::ImageHelper imageHelper(endpointAuthHelper.GetDatabaseHelper());
        auto photoData = imageHelper.GetSourcePhotoData(
            transaction, tableName, tableItemId);

        if (!photoData.has_value()) {
            resp = ErrorResponse::NotFound("No photo found for this item");
            return;
        }

        resp.code = 200;
        resp.set_header("Content-Type",
            "image/" + photoData->type);
        resp.set_header("Cache-Control", "public, max-age=86400");
        resp.body.assign(photoData->bytes.begin(), photoData->bytes.end());
    });
}

}  // namespace Endpoints
