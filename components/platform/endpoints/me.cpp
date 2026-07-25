#include "me.h"

#include "endpoints/endpoint_auth_helper.h"
#include "business_logic/auth/session.h"
#include "business_logic/auth/cookie_manager.h"
#include "util/error_response.h"

namespace Endpoints {
namespace {

// Phase 3.4 of the security review: /api/me is a read-only "who am I"
// status check, so it's a GET. This also keeps it outside the unsafe
// methods that the CSRF middleware (Phase 4) gates.
void HandleGet(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        Me(endpointAuthHelper, req, resp);
    }
    catch (...) {
        resp = ErrorResponse::NotAuthenticated();
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/me")
            .methods(crow::HTTPMethod::Get)(
                [=](const crow::request& req, crow::response& resp) {
                    HandleGet(webApp, req, resp);
                });
    }
} g_setupRouting;

} // namespace

bool Me(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp) {
    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::NotAuthenticated();
        return false;
    }

    bool success = false;
    try {
        tp->RunInTransaction([&](Transaction& transaction) {
            auto cookieManager = endpointAuthHelper.GetCookieManager();
            if (!cookieManager) {
                resp = ErrorResponse::NotAuthenticated();
                return;
            }

            // Attempt login via session cookie (auto login).
            if (!endpointAuthHelper.GetSession().InitializeFromFromCookie(
                transaction, cookieManager)) {
                resp = ErrorResponse::NotAuthenticated();
                return;
            }

            // Success
            resp = crow::response(200);
            success = true;
        });
    }
    catch (...) {
        resp = ErrorResponse::NotAuthenticated();
        return false;
    }
    return success;
}

} // namespace Endpoints