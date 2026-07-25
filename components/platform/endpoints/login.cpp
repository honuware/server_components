#include "login.h"

#include "endpoints/endpoint_auth_helper.h"
#include "business_logic/auth/login_gate.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/proxy_trust.h"
#include "business_logic/auth/session.h"
#include "business_logic/auth/cookie_manager.h"
#include "business_logic/auth/server_config.h"
#include "db_schema/auth_events.h"
#include "db_schema/login_attempts.h"
#include "sql_util/database_access/transaction_provider.h"
#include "sql_util/table_helpers/auth_events.h"
#include "sql_util/table_helpers/login_attempts.h"
#include "util/secrets/secret_keys.h"
#include "util/error_response.h"
#include "util/json_value.h"
#include "util/thread_pool.h"

namespace Endpoints {
namespace {

    void HandlePost(WebApp* webApp, const crow::request& req, crow::response& resp) {
        try {
            EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
            endpointAuthHelper.Initialize();
			if (req.body.empty()) {
				resp = ErrorResponse::BadRequest("No message body.");
				return;
			}
			Login(endpointAuthHelper, req, resp, Json::Value::FromText(req.body));
        }
        catch (std::exception& e) {
            resp = ErrorResponse::InvalidCredentials();
        }
        resp.end();
    }

    class SetupRouting : public RoutingBase {
    public:
        void AddRoute(WebApp* webApp) override {
            CROW_ROUTE(webApp->GetApp(), "/api/login")
                .methods(crow::HTTPMethod::POST)(
                    [=](const crow::request& req, crow::response& resp) {
                        HandlePost(webApp, req, resp);
                    });
        }
    } g_setupRouting;

    // Phase 5.4 of the security review: enqueue a fire-and-forget
    // login_attempts insert on the ThreadPool. Captures by value so
    // the lambda is independent of the per-request transaction; uses
    // the WebApp's TransactionProvider to open a fresh transaction.
    //
    // Tests that need the row to be visible call
    // ThreadPool::Shutdown() at the end of the test (which calls
    // Join under the hood) — same pattern as PersonHelper::SessionUsed.
    void EnqueueAttemptRecord(
        WebApp& webApp,
        const std::string& emailLower,
        const std::string& ip,
        const std::string& kind,
        bool success) {
        auto provider = webApp.GetTransactionProvider();
        if (!provider) return;
        auto db = webApp.GetDatabaseHelper();
        ThreadPool::GetInstance().Queue(
            [provider, db, emailLower, ip, kind, success]() {
                try {
                    provider->RunInTransaction([&](Transaction& tx) {
                        TableHelpers::LoginAttempts attempts(db);
                        attempts.RecordAttempt(
                            tx, emailLower, ip, kind, success);
                    });
                } catch (...) {
                    // The recorder is fire-and-forget — failures here
                    // (DB blip, etc.) must not crash the worker.
                }
            });
    }

} // namespace

void Login(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    const Json::Value& message) {
    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::InvalidCredentials();
        return;
    }
	if (!message.HasChildren()) {
        resp = ErrorResponse::InvalidCredentials();
        return;
    }

    // Validate required fields.
    const Json::Value* valueEmail = nullptr;
    const Json::Value* valuePassword = nullptr;
    const Json::Value* valueRemember = nullptr;

    if( !message.HasChild("email", &valueEmail) || !valueEmail->Is<std::string>() ||
        !message.HasChild("password", &valuePassword) || !valuePassword->Is<std::string>() ||
        !message.HasChild("remember", &valueRemember) || !valueRemember->Is<bool>()) {
        resp = ErrorResponse::InvalidCredentials();
        return;
    }
    std::string email = valueEmail->Get<std::string>();
    std::string password = valuePassword->Get<std::string>();
    bool remember = valueRemember->Get<bool>();

    // Phase 5.5 of the security review: best-effort client IP for
    // the gate's per-IP bucket.
    std::string clientIp = Auth::ResolveClientIp(req);

    tp->RunInTransaction([&](Transaction& transaction) {
        auto secrets = endpointAuthHelper.GetSecretsHelper();
        if (!secrets) {
            resp = ErrorResponse::InvalidCredentials();
            return;
        }

        // Phase 5.2 of the security review: rate-limit gate. Runs
        // BEFORE the Argon2id verify so a flooding attack stops at
        // the gate, not at the password hasher (~250ms saved per
        // refused attempt).
        Auth::LoginGate gate(endpointAuthHelper.GetDatabaseHelper());
        Auth::LoginGateResult gateResult = gate.CheckBeforeLogin(
            transaction, secrets, email, clientIp);
        if (gateResult != Auth::LoginGateResult::Allow) {
            // Phase 5.7 of the security review: generic 429 for any
            // gate refusal. The breakdown (which sub-bucket tripped)
            // shows up in the server log, not the response body.
            // Don't enqueue a login_attempts row for this case — the
            // attempt was rejected before it became an attempt; the
            // existing rows that pushed us past the threshold are
            // why we're here.
            resp = ErrorResponse::TooManyAttempts();
            return;
        }

        Auth::PersonHelper personHelper(endpointAuthHelper.GetDatabaseHelper());

        // Phase 5.3 of the security review: LoginPerson wraps
        // VerifyPassword with the sticky-lockout state machine.
        auto loginResult = personHelper.LoginPerson(
            transaction, secrets, email, password);

        // Capture the success flag before any further branching so
        // the async recorder gets the right value.
        bool credentialsValid = loginResult.success;

        // Phase 9.1: capture the User-Agent header (may be empty)
        // for the auth_events row. Read once here and reuse across
        // both branches.
        std::string userAgent = req.get_header_value("User-Agent");

        if (!credentialsValid) {
            // Phase 9.1 of the security review: forensic auth log.
            // Synchronous (same transaction as the verify) so a
            // later investigator sees the row even if the worker
            // thread is busy. login_failure carries the
            // attempted-email in detail_json since person_id is
            // NULL for unknown emails.
            //
            // CRITICAL: this SYNC INSERT must run BEFORE
            // EnqueueAttemptRecord. If Queue runs first the worker
            // thread can pick up the task immediately and open a
            // transaction on the SAME libpqxx connection (the
            // TestTransactionProvider routes back to the test's
            // shared transaction), racing with this synchronous
            // SQL. libpqxx connections aren't thread-safe so one
            // side throws "command already active" and the test
            // transaction aborts → the response that should be 401
            // becomes a generic InvalidCredentials AFTER an
            // exception thrown out of the transaction. Same race
            // exists on the success path below — see comment there.
            TableHelpers::AuthEvents authEvents(
                endpointAuthHelper.GetDatabaseHelper());
            // Build the detail JSON manually — a single string field
            // doesn't justify the JsonValue allocation overhead.
            std::string detail =
                "{\"email\":\"" + email + "\"}";
            authEvents.RecordEvent(transaction,
                DbSchema::kAuthEventsKindLoginFailure,
                loginResult.personId,
                clientIp, userAgent, detail);
            if (loginResult.justLocked) {
                // The attempt that crossed the lockout threshold —
                // record the discrete account_locked event so
                // alerting / admin reports can flag it specifically.
                authEvents.RecordEvent(transaction,
                    DbSchema::kAuthEventsKindAccountLocked,
                    loginResult.personId,
                    clientIp, userAgent, detail);
            }

            // Phase 5.4 of the security review: write-behind record.
            // Gives the gate accurate counts on the next attempt
            // without holding up THIS response. ThreadPool reads/
            // writes the email lowercased via CITEXT collation in
            // the table; passing the raw value is fine. Goes LAST
            // in this branch — see the "CRITICAL" comment above for
            // why nothing synchronous can follow it.
            EnqueueAttemptRecord(
                endpointAuthHelper.App(),
                email,
                clientIp,
                std::string(DbSchema::kLoginAttemptsKindLogin),
                /*success=*/false);

            resp = ErrorResponse::InvalidCredentials();
            return;
        }

        // Create session token
        std::string token;
        if (!personHelper.CreateSessionToken(transaction, secrets, email, token)) {
            // Treat as a credential failure for the response shape;
            // record as success-with-no-token won't help the gate.
            // This branch fires only on internal failures (DB issue,
            // missing secret) — surface as 401 to keep the auth
            // surface uniform.
            resp = ErrorResponse::InvalidCredentials();
            return;
        }

        // Create Session object and set cookie
        Auth::CookieManagerPtr cookieManager = endpointAuthHelper.GetCookieManager();
        if(!cookieManager) {
            resp = ErrorResponse::InvalidCredentials();
            return;
        }
        try {
            endpointAuthHelper.GetSession().InitializeFromLogin(
                transaction, secrets, token, cookieManager, email, remember);
        } catch (...) {
            resp = ErrorResponse::InvalidCredentials();
            return;
        }

        // redirect to website address secret
        std::string websiteAddress = secrets->LookupSecret(transaction, Secrets::kWebsiteAddress);
        if (websiteAddress.empty()) {
            resp = ErrorResponse::InvalidCredentials();
            return;
        }
        resp = crow::response(200);

        // Phase 9.1 of the security review: forensic auth log.
        // Synchronous (same transaction as the verify) so the row
        // is committed with the rest of the login transaction.
        //
        // CRITICAL: this SYNC INSERT runs BEFORE EnqueueAttemptRecord
        // to avoid racing with the worker thread on the test's
        // shared libpqxx connection. See the matching comment on
        // the failure branch above.
        TableHelpers::AuthEvents authEvents(
            endpointAuthHelper.GetDatabaseHelper());
        authEvents.RecordEvent(transaction,
            DbSchema::kAuthEventsKindLoginSuccess,
            loginResult.personId,
            clientIp, userAgent, /*detailJson=*/"");

        // Successful login. Record async — the row is informational
        // (resets the failure-count window for legitimate users).
        // Goes LAST in this branch; nothing synchronous can follow
        // a Queue call without racing on the connection.
        EnqueueAttemptRecord(
            endpointAuthHelper.App(),
            email,
            clientIp,
            std::string(DbSchema::kLoginAttemptsKindLogin),
            /*success=*/true);
    });
}

} // namespace Endpoints
