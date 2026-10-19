#include "forgot_password.h"

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

void HandlePost(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        if (req.body.empty()) {
            resp = ErrorResponse::BadRequest("Request body required");
        } else {
            ForgotPassword(endpointAuthHelper, req, resp, Json::Value::FromText(req.body));
        }
    }
    catch (std::exception& e) {
        // Never echo the error: one that happened only for a real account
        // would say the account exists. The text goes to the log.
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/forgot_password")
            .methods(crow::HTTPMethod::Post)(
                [=](const crow::request& req, crow::response& resp) {
                    HandlePost(webApp, req, resp);
                });
    }
} g_setupRouting;

}  // namespace

void ForgotPassword(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    const Json::Value& message) {
    const Json::Value* emailValue = nullptr;
    if (!message.HasChild("email", &emailValue) ||
        !emailValue->Is<std::string>() ||
        emailValue->Get<std::string>().empty()) {
        resp = ErrorResponse::ValidationError("email is required");
        return;
    }
    const std::string email = emailValue->Get<std::string>();
    const std::string clientIp = Auth::ResolveClientIp(req);
    const std::string userAgent = req.get_header_value("User-Agent");

    endpointAuthHelper.GetTransactionProvider()->RunInTransaction(
        [&](Transaction& transaction) {
            Auth::LoginGate gate(endpointAuthHelper.GetDatabaseHelper());
            if (gate.CheckBeforeForgotPassword(
                    transaction, endpointAuthHelper.GetSecretsHelper(),
                    email, clientIp) != Auth::LoginGateResult::Allow) {
                resp = ErrorResponse::TooManyAttempts();
                return;
            }

            // Queues the mail itself; that task never touches the database,
            // so the SQL below is still safe on this connection.
            Auth::PasswordResetHelper helper(endpointAuthHelper.GetDatabaseHelper());
            const int64_t personId = helper.RequestReset(
                transaction,
                endpointAuthHelper.GetSecretsHelper(),
                endpointAuthHelper.GetMailHelper(),
                email);
            if (personId > 0) {
                TableHelpers::AuthEvents authEvents(
                    endpointAuthHelper.GetDatabaseHelper());
                authEvents.RecordEvent(transaction,
                    DbSchema::kAuthEventsKindPasswordResetRequested,
                    personId, clientIp, userAgent, /*detailJson=*/"");
            }
            resp = crow::response(200);

            // Every request counts against the caps, known address or not.
            // Recorded write-behind like the other auth attempts, and LAST:
            // this task does use the database, and no synchronous SQL may
            // follow it on the shared connection.
            auto provider = endpointAuthHelper.GetTransactionProvider();
            auto db = endpointAuthHelper.GetDatabaseHelper();
            ThreadPool::GetInstance().Queue([provider, db, email, clientIp]() {
                try {
                    provider->RunInTransaction([&](Transaction& tx) {
                        TableHelpers::LoginAttempts attempts(db);
                        attempts.RecordAttempt(
                            tx, email, clientIp,
                            DbSchema::kLoginAttemptsKindPasswordResetRequest,
                            /*success=*/true);
                    });
                } catch (...) {
                    // Fire-and-forget — never crash the worker.
                }
            });
        });
}

}  // namespace Endpoints
