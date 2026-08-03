#include "delete_photo.h"

#include "endpoints/endpoint_auth_helper.h"
#include "business_logic/auth/session.h"
#include "business_logic/images/image_helper.h"
#include "util/error_response.h"

namespace Endpoints {
namespace {

void HandleDelete(
    WebApp* webApp,
    const crow::request& req,
    crow::response& resp,
    const std::string& tableName,
    int64_t tableItemId) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        DeletePhoto(endpointAuthHelper, req, resp, tableName, tableItemId);
    }
    catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/delete_photo/<string>/<int>")
            .methods(crow::HTTPMethod::POST)(
                [=](const crow::request& req, crow::response& resp,
                    const std::string& tableName, int64_t tableItemId) {
                    HandleDelete(webApp, req, resp, tableName, tableItemId);
                });
    }
} g_setupRouting;

}  // namespace

void DeletePhoto(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp,
    std::string_view tableName,
    int64_t tableItemId) {

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
        // Your own people photo is always yours to remove. Otherwise the same
        // rule as upload: an admin, or a non-admin whose permissions grant this
        // table through admin_table_permissions.
        bool ownPeoplePhoto =
            tableName == "people" && tableItemId == session.GetPersonId();
        if (!ownPeoplePhoto
            && !endpointAuthHelper.RequireTableWriteAccess(
                   transaction, tableName, resp)) {
            return;
        }

        Images::ImageHelper imageHelper(
            endpointAuthHelper.GetDatabaseHelper());
        bool deleted = imageHelper.DeletePhotoForItem(
            transaction, tableName, tableItemId);

        if (!deleted) {
            resp = ErrorResponse::NotFound("No photo found for this item");
            return;
        }

        resp.code = 200;
    });
}

}  // namespace Endpoints
