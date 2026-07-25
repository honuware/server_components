#include "account_activation.h"

#include "endpoints/endpoint_auth_helper.h"
#include "business_logic/auth/person.h"
#include "util/error_response.h"

namespace Endpoints {

namespace {

void HandlePost(
    WebApp* webApp,
    const crow::request& req,
    crow::response& resp,
    std::string_view email,
    std::string_view base64EncodedActivationToken) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        AccountActivation(endpointAuthHelper, email, base64EncodedActivationToken);
    }
    catch (std::exception& e) {
        resp = ErrorResponse::ValidationError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(
            webApp->GetApp(),
            "/api/account_activation/<string>/<string>")(
            [=](
                const crow::request& req,
                crow::response& resp,
                const std::string& email,
                const std::string& base64EncodedActivationToken) {
                HandlePost(
                    webApp,
                    req,
                    resp,
                    URLDecode(email),
                    URLDecode(base64EncodedActivationToken));
            });
    }
} g_setupRouting;

}  // namespace

void AccountActivation(
    EndpointAuthHelper& endpointAuthHelper,
    std::string_view email,
    std::string_view base64EncodedActivationToken) {
    endpointAuthHelper.GetTransactionProvider()->RunInTransaction([&](Transaction& transaction) {
        Auth::PersonHelper personHelper(endpointAuthHelper.GetDatabaseHelper());
        personHelper.VerifyPersonEmail(
            transaction,
            endpointAuthHelper.GetSecretsHelper(),
            email,
            base64EncodedActivationToken);

        // Phase 3.5 of the security review: removed a name-based admin
        // role grant ("if firstName == Mason || Tyler, assign admin"). It
        // was a bootstrap shortcut that became a privilege-escalation
        // path the moment any visitor figured out the rule. Admins are
        // now provisioned via seed data in create_database.cpp or via
        // the role-management UI — never via signup.

        // Redirect to login page
        crow::response& resp = endpointAuthHelper.Response();
        resp.code = 302;
        auto secrets = endpointAuthHelper.GetSecretsHelper();
        auto url = secrets->LookupSecret(
            transaction, Secrets::kWebsiteAddressLogin);
        url += secrets->LookupSecret(transaction, Secrets::kWebsiteLoginLink);
        resp.add_header("Location", url);
        });
}

}  // namespace Endpoints
