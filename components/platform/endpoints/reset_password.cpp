#include "reset_password.h"

#include "business_logic/auth/login_gate.h"
#include "business_logic/auth/password_reset_helper.h"
#include "business_logic/auth/proxy_trust.h"
#include "db_schema/auth_events.h"
#include "db_schema/login_attempts.h"
#include "sql_util/database_access/transaction_provider.h"
#include "sql_util/table_helpers/auth_events.h"
#include "sql_util/table_helpers/login_attempts.h"
#include "util/error_response.h"
#include "util/thread_pool.h"

namespace Endpoints {

namespace {

constexpr std::string_view kInvalidLink = "reset link is invalid or expired";

void HandlePost(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        if (req.body.empty()) {
            resp = ErrorResponse::BadRequest("Request body required");
        } else {
            ResetPassword(endpointAuthHelper, req, resp, Json::Value::FromText(req.body));
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
        CROW_ROUTE(webApp->GetApp(), "/api/reset_password")
            .methods(crow::HTTPMethod::Post)(
                [=](const crow::request& req, crow::response& resp) {
                    HandlePost(webApp, req, resp);
                });
    }
} g_setupRouting;

}  // namespace

void ResetPassword(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    const Json::Value& message) {
    const Json::Value* emailValue = nullptr;
    const Json::Value* secretValue = nullptr;
    const Json::Value* newPasswordValue = nullptr;
    if (!message.HasChild("email", &emailValue) ||
        !emailValue->Is<std::string>() ||
        emailValue->Get<std::string>().empty()) {
        resp = ErrorResponse::ValidationError("email is required");
        return;
    }
    if (!message.HasChild("secret", &secretValue) ||
        !secretValue->Is<std::string>() ||
        secretValue->Get<std::string>().empty()) {
        resp = ErrorResponse::ValidationError("secret is required");
        return;
    }
    // The rules registration enforces: a password is required.
    if (!message.HasChild("new_password", &newPasswordValue) ||
        !newPasswordValue->Is<std::string>() ||
        newPasswordValue->Get<std::string>().empty()) {
        resp = ErrorResponse::ValidationError("new_password is required");
        return;
    }
    const std::string email = emailValue->Get<std::string>();
    const std::string secret = secretValue->Get<std::string>();
    const std::string newPassword = newPasswordValue->Get<std::string>();
    const std::string clientIp = Auth::ResolveClientIp(req);
    const std::string userAgent = req.get_header_value("User-Agent");

    endpointAuthHelper.GetTransactionProvider()->RunInTransaction(
        [&](Transaction& transaction) {
            Auth::LoginGate gate(endpointAuthHelper.GetDatabaseHelper());
            if (gate.CheckBeforeResetPassword(
                    transaction, endpointAuthHelper.GetSecretsHelper(),
                    clientIp) != Auth::LoginGateResult::Allow) {
                resp = ErrorResponse::TooManyAttempts();
                return;
            }

            Auth::PasswordResetHelper helper(endpointAuthHelper.GetDatabaseHelper());
            const Auth::PasswordResetResult result = helper.CompleteReset(
                transaction,
                endpointAuthHelper.GetSecretsHelper(),
                email, secret, newPassword);
            const bool success =
                result.outcome == Auth::PasswordResetOutcome::Success;
            if (success) {
                TableHelpers::AuthEvents authEvents(
                    endpointAuthHelper.GetDatabaseHelper());
                authEvents.RecordEvent(transaction,
                    DbSchema::kAuthEventsKindPasswordResetCompleted,
                    result.personId, clientIp, userAgent, /*detailJson=*/"");
                resp = crow::response(200);
            } else if (result.outcome == Auth::PasswordResetOutcome::PasswordRejected) {
                resp = ErrorResponse::ValidationError("new_password is required");
            } else {
                resp = ErrorResponse::ValidationError(kInvalidLink);
            }

            // Recorded write-behind, LAST: no synchronous SQL may follow a
            // queued task that uses the database. Only failures count
            // toward the per-IP cap.
            auto provider = endpointAuthHelper.GetTransactionProvider();
            auto db = endpointAuthHelper.GetDatabaseHelper();
            ThreadPool::GetInstance().Queue([provider, db, email, clientIp, success]() {
                try {
                    provider->RunInTransaction([&](Transaction& tx) {
                        TableHelpers::LoginAttempts attempts(db);
                        attempts.RecordAttempt(
                            tx, email, clientIp,
                            DbSchema::kLoginAttemptsKindPasswordReset,
                            success);
                    });
                } catch (...) {
                    // Fire-and-forget — never crash the worker.
                }
            });
        });
}

}  // namespace Endpoints
