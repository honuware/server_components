#include "update_user_password.h"

#include "endpoints/endpoint_auth_helper.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/proxy_trust.h"
#include "business_logic/auth/session.h"
#include "db_schema/auth_events.h"
#include "db_schema/people.h"
#include "sql_util/table_helpers/auth_events.h"
#include "sql_util/table_helpers/people.h"
#include "util/error_response.h"
#include "util/json_value.h"
#include "util/types.h"

namespace Endpoints {
namespace {

void HandlePost(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();

        if (req.body.empty()) {
            resp = ErrorResponse::BadRequest("No message body.");
            resp.end();
            return;
        }

        UpdateUserPassword(endpointAuthHelper, req, resp, Json::Value::FromText(req.body));
    }
    catch (...) {
        resp = ErrorResponse::NotAuthenticated();
    }

    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/update_user_password")
            .methods(crow::HTTPMethod::POST)(
                [=](const crow::request& req, crow::response& resp) {
                    HandlePost(webApp, req, resp);
                });
    }
} g_setupRouting;

} // namespace

void UpdateUserPassword(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    const Json::Value& message) {
    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::NotAuthenticated();
        return;
    }

    if (!message.HasChildren()) {
        resp = ErrorResponse::BadRequest("Invalid JSON object.");
        return;
    }

    const auto& obj = message.GetChildren();
    auto itOld = obj.find("old_password");
    auto itNew = obj.find("new_password");

    if (itOld == obj.end() || itNew == obj.end() ||
        !itOld->second.Is<std::string>() || !itNew->second.Is<std::string>()) {
        resp = ErrorResponse::ValidationError("old_password and new_password are required.");
        return;
    }

    const std::string oldPassword = itOld->second.Get<std::string>();
    const std::string newPassword = itNew->second.Get<std::string>();

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

            // Lookup email from person record (like UpdateUserInfo flows).
            Auth::PersonInfo info;
            if (!personHelper.LookupPerson(transaction, personId, info)) {
                resp = ErrorResponse::NotAuthenticated();
                return;
            }

            // Validate old password using email.
            if (!personHelper.VerifyPassword(transaction, info.email, oldPassword)) {
                resp = ErrorResponse::InvalidCredentials("The current password you entered is incorrect");
                return;
            }

            // Pass the SecretsHelper so the Argon2id cost is driven by
            // kAuthArgon2OpsLimit / kAuthArgon2MemLimitKb rather than the
            // INTERACTIVE fallback. Phase 2.5 of the security review.
            personHelper.UpdatePassword(
                transaction,
                info.email,
                newPassword,
                endpointAuthHelper.GetSecretsHelper());

            // Clear must_change_password flag after successful password change
            TableHelpers::People people(endpointAuthHelper.GetDatabaseHelper());
            people.SetMustChangePassword(transaction, personId, false);

            // Phase 9.1: forensic audit row. Password changes are
            // high-signal — a flood of these against many accounts in
            // a short window is the signature of a takeover spree.
            TableHelpers::AuthEvents authEvents(
                endpointAuthHelper.GetDatabaseHelper());
            std::string clientIp = Auth::ResolveClientIp(req);
            std::string userAgent = req.get_header_value("User-Agent");
            authEvents.RecordEvent(transaction,
                DbSchema::kAuthEventsKindPasswordChanged,
                personId, clientIp, userAgent, /*detailJson=*/"");

            resp = crow::response(200);
        });
    }
    catch (...) {
        resp = ErrorResponse::NotAuthenticated();
    }
}

} // namespace Endpoints