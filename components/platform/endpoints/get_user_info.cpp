#include "get_user_info.h"

#include "endpoints/endpoint_auth_helper.h"
#include "business_logic/auth/session.h"
#include "business_logic/auth/person.h"
#include "db_schema/people.h"
#include "util/error_response.h"
#include "util/json_util.h"
#include "util/json_value.h"
#include "sql_util/table_helpers/people.h"

namespace Endpoints {
namespace {

void HandlePost(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        Json::Value result = GetUserInfo(endpointAuthHelper, req, resp);
        if (resp.code == 200) {
            resp.set_header("Content-Type", "application/json");
			resp.write(result.ToString());
        }
    }
    catch (...) {
        resp = ErrorResponse::NotAuthenticated();
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/get_user_info")
            .methods(crow::HTTPMethod::POST)(
                [=](const crow::request& req, crow::response& resp) {
                    HandlePost(webApp, req, resp);
                });
    }
} g_setupRouting;

} // namespace

Json::Value GetUserInfo(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp) {
    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::NotAuthenticated();
        return {};
    }

    Json::Value result;
    try {
        tp->RunInTransaction([&](Transaction& transaction) {
            // Must have a logged-in session
            Auth::Session& session = endpointAuthHelper.GetSession();
            if (!session.IsLoggedIn()) {
                resp = ErrorResponse::NotAuthenticated();
                return;
            }
            int personId = session.GetPersonId();
            if (personId <= 0) {
                resp = ErrorResponse::NotAuthenticated();
                return;
            }

            // Lookup person info
            Auth::PersonHelper personHelper(endpointAuthHelper.GetDatabaseHelper());
            TableHelpers::People people(endpointAuthHelper.GetDatabaseHelper());
            Auth::PersonInfo info;
            if (!personHelper.LookupPerson(transaction, personId, info)) {
                resp = ErrorResponse::NotAuthenticated();
                return;
            }

            // Build snake_case KeyValueTable
            KeyValueTable kv = {
                {"email", info.email},
                {"first_name", info.firstName},
                {"last_name", info.lastName},
            };

            // Augment with roles and permissions
            StringArray roles = personHelper.GetRolesForUser(transaction, personId);
            StringArray perms = personHelper.GetPermissionsForUser(transaction, personId);

            // Check must_change_password flag
            KeyValueTable personKv = people.GetPersonById(transaction, personId);
            bool mustChangePassword = false;
            auto mcpIt = personKv.find(std::string(DbSchema::kPeopleMustChangePassword));
            if (mcpIt != personKv.end()) {
                mustChangePassword = (mcpIt->second == "t" || mcpIt->second == "true");
            }

            result = Json::Value(kv);
            result["person_id"] = static_cast<int64_t>(personId);
            result["roles"] = roles;
            result["permissions"] = perms;
            result["created_at"] = people.GetCreatedAt(transaction, personId);
            result["must_change_password"] = mustChangePassword;

            resp.code = 200;
        });
    }
    catch (...) {
        resp = ErrorResponse::NotAuthenticated();
        return {};
    }
    return result;
}

} // namespace Endpoints