#include "set_user_info.h"

#include "endpoints/endpoint_auth_helper.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/session.h"
#include "util/error_response.h"
#include "util/json_value.h"

namespace Endpoints {
namespace {

void HandlePost(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();

        if (req.body.empty()) {
            // Missing fields are not an error, but missing body isn't useful either.
            // Keep behavior consistent with other endpoints.
            resp = ErrorResponse::BadRequest("No message body.");
            resp.end();
            return;
        }

        SetUserInfo(endpointAuthHelper, req, resp, Json::Value::FromText(req.body));
    }
    catch (...) {
        resp = ErrorResponse::NotAuthenticated();
    }

    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/set_user_info")
            .methods(crow::HTTPMethod::POST)(
                [=](const crow::request& req, crow::response& resp) {
                    HandlePost(webApp, req, resp);
                });
    }
} g_setupRouting;

} // namespace

void SetUserInfo(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp,
    const Json::Value& message) {
    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::NotAuthenticated();
        return;
    }

    // We require a JSON object, but fields are optional.
    if (!message.HasChildren()) {
        resp = ErrorResponse::BadRequest("Invalid JSON object.");
        return;
    }

    try {
        tp->RunInTransaction([&](Transaction& transaction) {
            Auth::Session& session = endpointAuthHelper.GetSession();
            if (!session.IsLoggedIn()) {
                resp = ErrorResponse::NotAuthenticated();
                return;
            }

            const int personId = session.GetPersonId();
            if (personId <= 0) {
                resp = ErrorResponse::NotAuthenticated();
                return;
            }

            Auth::PersonHelper personHelper(endpointAuthHelper.GetDatabaseHelper());

            // Start with current data so UpdateInfo can update the subset we change.
            Auth::PersonInfo info;
            if (!personHelper.LookupPerson(transaction, personId, info)) {
                resp = ErrorResponse::NotAuthenticated();
                return;
            }

            std::string email = info.email;

            const auto& obj = message.GetChildren();

            // Missing fields are NOT an error.
            auto itEmail = obj.find("email");
            if (itEmail != obj.end() && itEmail->second.Is<std::string>()) {
                info.email = itEmail->second.Get<std::string>();
            }

            auto itFirst = obj.find("first_name");
            if (itFirst != obj.end() && itFirst->second.Is<std::string>()) {
                info.firstName = itFirst->second.Get<std::string>();
            }

            auto itLast = obj.find("last_name");
            if (itLast != obj.end() && itLast->second.Is<std::string>()) {
                info.lastName = itLast->second.Get<std::string>();
            }

            personHelper.UpdateInfo(transaction, email, info);

            resp = crow::response(200);
        });
    }
    catch (...) {
        resp = ErrorResponse::NotAuthenticated();
    }
}

} // namespace Endpoints