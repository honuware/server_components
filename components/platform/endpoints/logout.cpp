#include "logout.h"

#include "endpoints/endpoint_auth_helper.h"
#include "business_logic/auth/auth_cookies.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/proxy_trust.h"
#include "business_logic/auth/session.h"
#include "business_logic/auth/cookie_manager.h"
#include "db_schema/auth_events.h"
#include "db_schema/device_tokens.h"
#include "sql_util/table_helpers/auth_events.h"
#include "util/secrets/secret_keys.h"

namespace Endpoints {
namespace {

void HandlePost(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        Logout(endpointAuthHelper, req, resp);
    }
    catch (...) {
        resp = crow::response(200);
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/logout")
            .methods(crow::HTTPMethod::POST)(
                [=](const crow::request& req, crow::response& resp) {
                    HandlePost(webApp, req, resp);
                });
    }
} g_setupRouting;

} // namespace

void Logout(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp) {
    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        // Still succeed
        resp = crow::response(200);
        return;
    }

    // Phase 9.1: capture the request context once for the auth_events row.
    std::string clientIp = Auth::ResolveClientIp(req);
    std::string userAgent = req.get_header_value("User-Agent");

    try {
        tp->RunInTransaction([&](Transaction& transaction) {
            auto cookieManager = endpointAuthHelper.GetCookieManager();
            if (cookieManager) {
                int64_t personId = -1;

                // Try to resolve person via session token
                auto cookies = cookieManager->GetCookies();
                auto itSess = cookies.find("session_token");
                if (itSess != cookies.end()) {
                    Auth::PersonHelper helper(endpointAuthHelper.GetDatabaseHelper());
                    int64_t outId = -1;
                    if (helper.LookupPersonIdBySessionToken(transaction, itSess->second, outId)) {
                        personId = outId;
                    }
                }

                // If not found, attempt to resolve via device token (uuid.secret)
                if (personId <= 0) {
                    auto itDev = cookies.find("device_token");
                    if (itDev != cookies.end()) {
                        const std::string& combined = itDev->second;
                        size_t dot = combined.find('.');
                        if (dot != std::string::npos) {
                            std::string uuid = combined.substr(0, dot);
                            // Direct DB lookup for person_id
                            KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
                                "SELECT * FROM device_tokens WHERE uuid = $1", uuid);
                            if (!row.empty()) {
                                try {
                                    personId = std::stoll(row.at(std::string(DbSchema::kDeviceTokensPersonId)));
                                } catch (...) { /* ignore */ }
                            }
                        }
                    }
                }

                // Perform logout if we resolved a user
                if (personId > 0) {
                    Auth::PersonHelper helper(endpointAuthHelper.GetDatabaseHelper());
                    helper.LogoutPerson(transaction, personId);

                    // Phase 9.1 of the security review: forensic
                    // auth log. Only emit when we actually identified
                    // the user — a logout with no resolvable session
                    // is just a no-op response, not a real event.
                    TableHelpers::AuthEvents authEvents(
                        endpointAuthHelper.GetDatabaseHelper());
                    authEvents.RecordEvent(transaction,
                        DbSchema::kAuthEventsKindLogout,
                        personId, clientIp, userAgent, "");
                }

                // Phase 6.1 of the security review: clear all three
                // auth cookies via the shared helper, so the clear
                // attributes are identical to the set attributes.
                // The previous inlined version used SameSite=None
                // at clear-time even though session.cpp sets them
                // with SameSite=Lax — browsers won't apply a delete
                // when those disagree.
                Auth::ClearAuthCookies(
                    transaction,
                    endpointAuthHelper.GetSecretsHelper(),
                    cookieManager);
            }

            // Always succeed
            resp = crow::response(200);
        });
    }
    catch (...) {
        // Always succeed
        resp = crow::response(200);
    }
}

} // namespace Endpoints