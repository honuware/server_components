#include "verify.h"

#include "endpoints/endpoint_auth_helper.h"
#include <stdexcept>
#include "business_logic/auth/login_gate.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/proxy_trust.h"
#include "db_schema/auth_events.h"
#include "db_schema/login_attempts.h"
#include "db_schema/people.h"
#include "sql_util/database_access/transaction_provider.h"
#include "sql_util/table_helpers/auth_events.h"
#include "sql_util/table_helpers/login_attempts.h"
#include "sql_util/table_helpers/people.h"
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
            return;
        }
        Verify(endpointAuthHelper, req, resp, Json::Value::FromText(req.body));
    }
    catch (std::exception& e) {
        resp = ErrorResponse::ValidationError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/verify")
            .methods(crow::HTTPMethod::POST)(
                [=](const crow::request& req, crow::response& resp) {
                    HandlePost(webApp, req, resp);
                });
    }
} g_setupRouting;

// Phase 5.4 / 5.6: write-behind recorder for verify attempts.
// Email goes in for forensic value (so a security-incident report
// can answer "which addresses got brute-forced") but the gate's
// per-IP query ignores email anyway.
void EnqueueVerifyAttempt(
    WebApp& webApp,
    const std::string& emailLower,
    const std::string& ip,
    bool success) {
    auto provider = webApp.GetTransactionProvider();
    if (!provider) return;
    auto db = webApp.GetDatabaseHelper();
    ThreadPool::GetInstance().Queue(
        [provider, db, emailLower, ip, success]() {
            try {
                provider->RunInTransaction([&](Transaction& tx) {
                    TableHelpers::LoginAttempts attempts(db);
                    attempts.RecordAttempt(
                        tx, emailLower, ip,
                        DbSchema::kLoginAttemptsKindVerify,
                        success);
                });
            } catch (...) {
                // Fire-and-forget — never crash the worker.
            }
        });
}

}  // namespace

void Verify(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    const Json::Value& message) {
    if (!message.HasChildren()) {
        resp = ErrorResponse::ValidationError("Request body required");
        return;
    }

    const Json::Value* emailValue = nullptr;
    const Json::Value* secretValue = nullptr;
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

    std::string email = emailValue->Get<std::string>();
    std::string encodedSecret = secretValue->Get<std::string>();
    // Phase 5.5: best-effort client IP for the gate's per-IP bucket.
    std::string clientIp = Auth::ResolveClientIp(req);
    // Phase 9.1: forensic logging context.
    std::string userAgent = req.get_header_value("User-Agent");

    endpointAuthHelper.GetTransactionProvider()->RunInTransaction(
        [&](Transaction& transaction) {
            // Phase 5.6: per-IP gate. Verify is a pure write
            // operation gated by a one-shot secret; an attacker
            // who can't read the email inbox can only brute-force
            // the secret. The per-IP cap is the only useful gate.
            Auth::LoginGate gate(endpointAuthHelper.GetDatabaseHelper());
            if (gate.CheckBeforeVerify(
                    transaction,
                    endpointAuthHelper.GetSecretsHelper(),
                    clientIp) != Auth::LoginGateResult::Allow) {
                resp = ErrorResponse::TooManyAttempts();
                return;
            }

            bool success = false;
            try {
                Auth::PersonHelper personHelper(
                    endpointAuthHelper.GetDatabaseHelper());
                personHelper.VerifyPersonEmail(
                    transaction,
                    endpointAuthHelper.GetSecretsHelper(),
                    email,
                    encodedSecret);
                success = true;
                resp = crow::response(200);
            } catch (const std::exception&) {
                // Don't echo the underlying error — verification failures
                // (wrong secret, expired, already-verified) all collapse
                // to the same generic 400 so a caller can't distinguish
                // them. Phase 3.3 of the security review.
                resp = ErrorResponse::ValidationError(
                    "verification failed or expired");
            }

            // Phase 9.1: record success in auth_events for forensic
            // audit. Failure path is intentionally NOT logged here:
            // unverified-email login is its own login_failure entry,
            // and verify failures are dominated by stale/expired
            // secrets which are noise.
            //
            // CRITICAL: this SYNC INSERT must run BEFORE
            // EnqueueVerifyAttempt below. Once a task is queued the
            // worker may pick it up immediately and open a
            // transaction on the SAME libpqxx connection the test
            // is using (the TestTransactionProvider returns the
            // shared transaction). libpqxx connections are NOT
            // thread-safe — concurrent SQL throws "command already
            // active" and the test's transaction aborts. Putting
            // all sync SQL before Queue avoids the race.
            if (success) {
                int64_t personId = 0;
                try {
                    TableHelpers::People people(
                        endpointAuthHelper.GetDatabaseHelper());
                    KeyValueTable row = people.LookupPersonByEmail(
                        transaction, email);
                    auto it = row.find(std::string(DbSchema::kPeopleId));
                    if (it != row.end() && !it->second.empty()) {
                        personId = std::stoll(it->second);
                    }
                } catch (...) {
                    // Fall through with personId == 0 (logged as NULL).
                }
                TableHelpers::AuthEvents authEvents(
                    endpointAuthHelper.GetDatabaseHelper());
                std::string detail = "{\"email\":\"" + email + "\"}";
                authEvents.RecordEvent(transaction,
                    DbSchema::kAuthEventsKindEmailVerified,
                    personId, clientIp, userAgent, detail);
            }

            // Phase 5.4 / 5.6: record outcome async. MUST go LAST —
            // nothing synchronous can follow this without racing
            // on the shared libpqxx connection.
            EnqueueVerifyAttempt(
                endpointAuthHelper.App(),
                email, clientIp, success);
        });
}

}  // namespace Endpoints
