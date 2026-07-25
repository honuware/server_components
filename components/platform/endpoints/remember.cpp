#include "remember.h"

#include "endpoints/endpoint_auth_helper.h"
#include "business_logic/auth/login_gate.h"
#include "business_logic/auth/proxy_trust.h"
#include "business_logic/auth/session.h"
#include "business_logic/auth/cookie_manager.h"
#include "db_schema/login_attempts.h"
#include "sql_util/database_access/transaction_provider.h"
#include "sql_util/table_helpers/login_attempts.h"
#include "util/secrets/secret_keys.h"
#include "util/error_response.h"
#include "util/thread_pool.h"

namespace Endpoints {
namespace {

void HandlePost(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        Remember(endpointAuthHelper, req, resp);
    }
    catch (...) {
        resp = ErrorResponse::NotAuthenticated();
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/remember")
            .methods(crow::HTTPMethod::POST)(
                [=](const crow::request& req, crow::response& resp) {
                    HandlePost(webApp, req, resp);
                });
    }
} g_setupRouting;

// Phase 5.4 / 5.6: write-behind recorder for remember-me attempts.
// We don't have an authenticated email here — the device token IS
// the credential. Pass empty for emailLower; the gate is per-IP only.
void EnqueueRememberAttempt(
    WebApp& webApp,
    const std::string& ip,
    bool success) {
    auto provider = webApp.GetTransactionProvider();
    if (!provider) return;
    auto db = webApp.GetDatabaseHelper();
    ThreadPool::GetInstance().Queue(
        [provider, db, ip, success]() {
            try {
                provider->RunInTransaction([&](Transaction& tx) {
                    TableHelpers::LoginAttempts attempts(db);
                    attempts.RecordAttempt(
                        tx, /*emailLower=*/"", ip,
                        DbSchema::kLoginAttemptsKindRemember,
                        success);
                });
            } catch (...) {
                // Fire-and-forget — never crash the worker.
            }
        });
}

} // namespace

bool Remember(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp) {
    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::NotAuthenticated();
        return false;
    }

    // Phase 5.5: best-effort client IP for the per-IP gate bucket.
    std::string clientIp = Auth::ResolveClientIp(req);

    bool success = false;
    try {
        tp->RunInTransaction([&](Transaction& transaction) {
            auto secrets = endpointAuthHelper.GetSecretsHelper();
            auto cookieManager = endpointAuthHelper.GetCookieManager();
            if (!secrets || !cookieManager) {
                resp = ErrorResponse::NotAuthenticated();
                return;
            }

            // Phase 5.6: per-IP gate for remember-me. Same shape as
            // verify — there's no authenticated email at this
            // point, so per-IP is the only useful bucket.
            Auth::LoginGate gate(endpointAuthHelper.GetDatabaseHelper());
            if (gate.CheckBeforeRemember(transaction, secrets, clientIp)
                    != Auth::LoginGateResult::Allow) {
                resp = ErrorResponse::TooManyAttempts();
                return;
            }

            // Attempt login via device token (remember me).
            if (!endpointAuthHelper.GetSession().InitializeFromDeviceToken(
                transaction, secrets, cookieManager)) {
                resp = ErrorResponse::NotAuthenticated();
                EnqueueRememberAttempt(
                    endpointAuthHelper.App(), clientIp, /*success=*/false);
                return;
            }

            // Success
            resp = crow::response(200);
            success = true;
            EnqueueRememberAttempt(
                endpointAuthHelper.App(), clientIp, /*success=*/true);
        });
    }
    catch (...) {
        resp = ErrorResponse::NotAuthenticated();
        return false;
    }
    return success;
}

} // namespace Endpoints
