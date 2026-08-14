#include "has_photo.h"

#include "endpoints/endpoint_auth_helper.h"
#include "business_logic/auth/session.h"
#include "business_logic/images/image_helper.h"
#include "util/error_response.h"
#include "util/json_value.h"

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
        Json::Value result = GetHasPhoto(
            endpointAuthHelper, req, resp, tableName, tableItemId);
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
        CROW_ROUTE(webApp->GetApp(), "/api/has_photo/<string>/<int>")
            .methods(crow::HTTPMethod::GET)(
                [=](const crow::request& req, crow::response& resp,
                    const std::string& tableName, int64_t tableItemId) {
                    HandleGet(webApp, req, resp, tableName, tableItemId);
                });
    }
} g_setupRouting;

}  // namespace

Json::Value GetHasPhoto(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp,
    std::string_view tableName,
    int64_t tableItemId) {

    Auth::Session& session = endpointAuthHelper.GetSession();
    if (!session.IsLoggedIn()) {
        resp = ErrorResponse::NotAuthenticated("Login required");
        return {};
    }

    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::InternalError("Database unavailable");
        return {};
    }

    Json::Value result;
    tp->RunInTransaction([&](Transaction& transaction) {
        Images::ImageHelper imageHelper(endpointAuthHelper.GetDatabaseHelper());
        // One query answers both "is there one" and "how big is it" — the pair
        // a management list actually asks. Reads no image bytes (Phase 6B).
        Images::ImageHelper::PhotoDimensions dimensions =
            imageHelper.GetPhotoDimensions(transaction, tableName, tableItemId);

        result = Json::Value(Json::JsonObject{
            {"has_photo", Json::Value(dimensions.found)},
            {"width", Json::Value(static_cast<int64_t>(dimensions.width))},
            {"height", Json::Value(static_cast<int64_t>(dimensions.height))},
            {"type", Json::Value(dimensions.type)},
        });
        resp.code = 200;
    });

    return result;
}

}  // namespace Endpoints
