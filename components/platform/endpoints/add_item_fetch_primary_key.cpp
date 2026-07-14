#include "add_item_fetch_primary_key.h"

#include "endpoint_auth_helper.h"
#include "sql_util/json/database_rest_helper.h"
#include "sql_util/database_access/database_metadata.h"
#include "util/error_response.h"

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
                Json::Value result = AddItemFetchPrimaryKey(endpointAuthHelper, Json::Value::FromText(req.body));
                resp.code = 200;
                resp.set_header("Content-Type", "application/json");
				resp.write(result.ToString());
            }
            catch (std::invalid_argument& e) {
                // Phase 7.3 of the security review: input-validation
                // failures (missing fields, disallowed tables, etc.)
                // surface as 400. The detail text comes from our own
                // helpers — not user input — so it's safe to echo
                // and helpful to the SPA.
                resp = ErrorResponse::ValidationError(e.what());
            }
            catch (std::exception& e) {
                // Phase 7.3 of the security review: anything else is
                // an unexpected server-side failure — log the
                // exception text but return the generic 500 body.
                resp = ErrorResponse::InternalError(e.what());
            }
            resp.end();
        }

        class SetupRouting : public RoutingBase {
        public:
            void AddRoute(WebApp* webApp) override {
                CROW_ROUTE(webApp->GetApp(), "/api/add_item_fetch_primary_key")
                    .methods(crow::HTTPMethod::POST)(
                        [=](const crow::request& req, crow::response& resp) {
                            HandlePost(webApp, req, resp);
                        });
            }
        } g_setupRouting;

    }  // namespace

    Json::Value AddItemFetchPrimaryKey(
        EndpointAuthHelper& endpointAuthHelper,
        const Json::Value& message) {
        Json::Value result;
        endpointAuthHelper.GetTransactionProvider()->RunInTransaction([&](Transaction& transaction) {
			if (!message.HasChildren()) {
				throw std::invalid_argument("Invalid JSON object.");
			}
            const Json::Value* tableNameValue = nullptr;
            const Json::Value* valueValue = nullptr;
            if (!message.HasChild("table_name", &tableNameValue)) {
                throw std::invalid_argument("table_name not found.");
            }
            if (!message.HasChild("value", &valueValue)) {
                throw std::invalid_argument("value not found.");
            }
            if (!tableNameValue->Is<std::string>()) {
                throw std::invalid_argument("table_name must be a string.");
            }
            std::string tableName = tableNameValue->Get<std::string>();
            if (!endpointAuthHelper.IsTableAllowed(transaction, tableName)) {
                std::stringstream ss;
                ss << "Table(" << tableName << ") is not an allowed table.";
                throw std::invalid_argument(ss.str());
            }

            DatabaseRESTHelper restHelper(endpointAuthHelper.GetDatabaseHelper());
            result = restHelper.AddTableValueFetchPrimaryKey(
				transaction, endpointAuthHelper.GetDatabaseInfo(), tableName, *valueValue);
            });
        return result;
    }

}  // namespace Endpoints