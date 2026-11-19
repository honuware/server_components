#include "image_upload_settings.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "business_logic/auth/session.h"
#include "business_logic/images/image_upload_limit.h"
#include "db_schema/permissions.h"
#include "endpoints/endpoint_auth_helper.h"
#include "util/error_response.h"
#include "util/json_value.h"

namespace Endpoints {
namespace {

constexpr std::string_view kLimitField = "max_upload_bytes";

using Handler = Json::Value (*)(EndpointAuthHelper&, const crow::request&, crow::response&);

void Handle(WebApp* webApp, const crow::request& req, crow::response& resp, Handler handler) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        Json::Value result = handler(endpointAuthHelper, req, resp);
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
        CROW_ROUTE(webApp->GetApp(), "/api/image_upload_limit")
            .methods(crow::HTTPMethod::Get)(
                [=](const crow::request& req, crow::response& resp) {
                    Handle(webApp, req, resp, &GetImageUploadLimit);
                });
        CROW_ROUTE(webApp->GetApp(), "/api/admin/image_upload_settings")
            .methods(crow::HTTPMethod::Get)(
                [=](const crow::request& req, crow::response& resp) {
                    Handle(webApp, req, resp, &GetImageUploadSettings);
                });
        CROW_ROUTE(webApp->GetApp(), "/api/admin/image_upload_settings")
            .methods(crow::HTTPMethod::Put)(
                [=](const crow::request& req, crow::response& resp) {
                    Handle(webApp, req, resp, &PutImageUploadSettings);
                });
    }
} g_setupRouting;

Json::Value SettingsBody(int64_t limitBytes) {
    return Json::Value(Json::JsonObject{
        {std::string(kLimitField), Json::Value(limitBytes)},
        {"default_bytes", Json::Value(Images::kImageUploadLimitDefaultBytes)},
        {"minimum_bytes", Json::Value(Images::kImageUploadLimitMinimumBytes)},
        {"maximum_bytes", Json::Value(Images::kImageUploadLimitMaximumBytes)},
    });
}

}  // namespace

Json::Value GetImageUploadLimit(
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
        if (!endpointAuthHelper.GetSession().IsLoggedIn()) {
            resp = ErrorResponse::NotAuthenticated("Login required");
            return;
        }
        result = Json::Value(Json::JsonObject{
            {std::string(kLimitField),
             Json::Value(Images::LoadImageUploadLimit(
                 transaction, endpointAuthHelper.GetSecretsHelper()))},
        });
        resp.code = 200;
    });
    return result;
}

Json::Value GetImageUploadSettings(
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
        if (!endpointAuthHelper.RequirePermission(
                transaction, DbSchema::kPermissionAdminPortal, resp)) {
            return;
        }
        result = SettingsBody(Images::LoadImageUploadLimit(
            transaction, endpointAuthHelper.GetSecretsHelper()));
        resp.code = 200;
    });
    return result;
}

Json::Value PutImageUploadSettings(
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
        if (!endpointAuthHelper.RequirePermission(
                transaction, DbSchema::kPermissionAdminPortal, resp)) {
            return;
        }
        // A body that isn't JSON is refused like one without the limit (a 400,
        // not the 500 a parse error would otherwise become).
        Json::Value body;
        try {
            body = Json::Value::FromText(req.body);
        } catch (const std::exception&) {
            body = Json::Value();
        }
        const Json::Value* field = nullptr;
        const int64_t* bytes = nullptr;
        if (body.HasChild(kLimitField, &field)) {
            bytes = field->TryGet<int64_t>();
        }
        if (bytes == nullptr) {
            resp = ErrorResponse::ValidationError(
                "The upload limit must be a whole number of bytes.", kLimitField);
            return;
        }
        const std::string problem = Images::ValidateImageUploadLimit(*bytes);
        if (!problem.empty()) {
            resp = ErrorResponse::ValidationError(problem, kLimitField);
            return;
        }
        Images::SaveImageUploadLimit(transaction, endpointAuthHelper.GetSecretsHelper(), *bytes);
        result = SettingsBody(*bytes);
        resp.code = 200;
    });
    return result;
}

}  // namespace Endpoints
