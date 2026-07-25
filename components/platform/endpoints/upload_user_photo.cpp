#include "upload_user_photo.h"

#include "endpoints/endpoint_auth_helper.h"
#include "business_logic/auth/session.h"
#include "business_logic/images/image_helper.h"
#include "business_logic/images/image_key_value_table.h"
#include "util/secrets/secret_keys.h"
#include "sql_util/json/key_value_table_json.h"
#include "util/error_response.h"
#include "util/json_value.h"

namespace Endpoints {
namespace {

void HandlePost(
    WebApp* webApp,
    const crow::request& req,
    crow::response& resp,
    const std::string& imageType) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        Json::Value result = PostUploadUserPhoto(
            endpointAuthHelper, req, resp, imageType);
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
        CROW_ROUTE(webApp->GetApp(), "/api/upload_user_photo/<string>")
            .methods(crow::HTTPMethod::POST)(
                [=](const crow::request& req, crow::response& resp,
                    const std::string& imageType) {
                    HandlePost(webApp, req, resp, imageType);
                });
    }
} g_setupRouting;

}  // namespace

Json::Value PostUploadUserPhoto(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    std::string_view imageType) {

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

    // Validate image type
    if (imageType != "jpeg" && imageType != "png") {
        resp = ErrorResponse::BadRequest(
            "Unsupported image type. Must be 'jpeg' or 'png'");
        return {};
    }

    if (req.body.empty()) {
        resp = ErrorResponse::BadRequest("No image data in request body");
        return {};
    }

    Json::Value result;
    tp->RunInTransaction([&](Transaction& transaction) {
        // Check upload size limit
        auto secrets = endpointAuthHelper.GetSecretsHelper();
        if (secrets) {
            std::string maxBytesStr = secrets->LookupSecret(
                transaction, Secrets::kImageMaxUploadBytes);
            if (!maxBytesStr.empty()) {
                int64_t maxBytes = std::stoll(maxBytesStr);
                if (static_cast<int64_t>(req.body.size()) > maxBytes) {
                    resp = ErrorResponse::BadRequest(
                        "Image exceeds maximum upload size");
                    return;
                }
            }
        }

        int64_t personId = session.GetPersonId();
        std::vector<char> imageBytes(req.body.begin(), req.body.end());
        Images::ImageHelper imageHelper(
            endpointAuthHelper.GetDatabaseHelper(),
            endpointAuthHelper.GetSecretsHelper());
        auto uploadResult = imageHelper.UploadAndAssociatePhoto(
            transaction, "people", personId, imageBytes, imageType);

        if (!uploadResult.success) {
            resp = ErrorResponse::BadRequest(uploadResult.errorMessage);
            return;
        }

        auto kvTable = Images::SourcePhotoInfoToKeyValueTable(
            uploadResult.sourcePhoto);
        result = SqlUtil::KeyValueTableToJson(kvTable);
        resp.code = 200;
    });

    return result;
}

}  // namespace Endpoints
