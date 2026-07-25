#include "register.h"

#include "endpoints/endpoint_auth_helper.h"
#include "endpoints/registration_hooks.h"
#include "business_logic/auth/person.h"
#include "db_schema/people.h"
#include "sql_util/table_helpers/people.h"
#include "util/error_response.h"
#include "util/json_value.h"

namespace Endpoints {

namespace {

// Phase 3.1 of the security review: register used to be
// `/api/register/<firstName>/<lastName>/<email>/<password>` — passwords in
// URL paths leak into access logs (CloudFront, any reverse proxy),
// browser history, the `Referer` header on outbound links, and Crow's
// own request URL field. The endpoint is now POST with a JSON body and
// the password never appears in a URL.
void HandlePost(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        if (req.body.empty()) {
            resp = ErrorResponse::BadRequest("Request body required");
            return;
        }
        Register(endpointAuthHelper, req, resp, Json::Value::FromText(req.body));
    }
    catch (std::exception& e) {
        resp = ErrorResponse::ValidationError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/register")
            .methods(crow::HTTPMethod::POST)(
                [=](const crow::request& req, crow::response& resp) {
                    HandlePost(webApp, req, resp);
                });
    }
} g_setupRouting;

}  // namespace

void Register(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp,
    const Json::Value& message) {
    if (!message.HasChildren()) {
        resp = ErrorResponse::ValidationError("Request body required");
        return;
    }

    // Validate every field up front so a missing one returns a clean
    // 422 before we touch the database. We don't echo the field name
    // beyond what's structurally needed; the message just says "field
    // required".
    const Json::Value* firstNameValue = nullptr;
    const Json::Value* lastNameValue = nullptr;
    const Json::Value* emailValue = nullptr;
    const Json::Value* passwordValue = nullptr;
    if (!message.HasChild("first_name", &firstNameValue) ||
        !firstNameValue->Is<std::string>() ||
        firstNameValue->Get<std::string>().empty()) {
        resp = ErrorResponse::ValidationError("first_name is required");
        return;
    }
    if (!message.HasChild("last_name", &lastNameValue) ||
        !lastNameValue->Is<std::string>() ||
        lastNameValue->Get<std::string>().empty()) {
        resp = ErrorResponse::ValidationError("last_name is required");
        return;
    }
    if (!message.HasChild("email", &emailValue) ||
        !emailValue->Is<std::string>() ||
        emailValue->Get<std::string>().empty()) {
        resp = ErrorResponse::ValidationError("email is required");
        return;
    }
    if (!message.HasChild("password", &passwordValue) ||
        !passwordValue->Is<std::string>() ||
        passwordValue->Get<std::string>().empty()) {
        resp = ErrorResponse::ValidationError("password is required");
        return;
    }

    std::string firstName = firstNameValue->Get<std::string>();
    std::string lastName = lastNameValue->Get<std::string>();
    std::string email = emailValue->Get<std::string>();
    std::string password = passwordValue->Get<std::string>();

    endpointAuthHelper.GetTransactionProvider()->RunInTransaction(
        [&](Transaction& transaction) {
            Auth::PersonHelper personHelper(
                endpointAuthHelper.GetDatabaseHelper());
            Auth::PersonInfo info{ email, firstName, lastName };
            personHelper.PreliminaryRegisterPerson(
                transaction,
                endpointAuthHelper.GetSecretsHelper(),
                endpointAuthHelper.GetMailHelper(),
                info,
                password,
                true);

            // Best-effort app-supplied post-registration hook (e.g. converting
            // pending gift-permission invitations). Registered by the app via
            // WebApp::SetService<Endpoints::PostRegisterHook>; absent for consumers
            // with no post-register step. This must NEVER block registration.
            if (auto hook = endpointAuthHelper.GetService<PostRegisterHook>();
                hook && hook->onRegistered) {
                try {
                    TableHelpers::People people(endpointAuthHelper.GetDatabaseHelper());
                    KeyValueTable personKv = people.LookupPersonByEmail(transaction, email);
                    if (!personKv.empty()) {
                        int64_t newPersonId = std::stoll(
                            personKv.at(std::string(DbSchema::kPeopleId)));
                        hook->onRegistered(
                            endpointAuthHelper, transaction, newPersonId, email);
                    }
                } catch (const std::exception&) {
                    // Swallow — post-register follow-up must not prevent registration
                }
            }

            resp = crow::response(200);
        });
}

}  // namespace Endpoints
