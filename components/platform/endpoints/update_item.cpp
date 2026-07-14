#include "update_item.h"

#include "endpoint_auth_helper.h"
#include "sql_util/json/database_rest_helper.h"
#include "sql_util/database_access/database_metadata.h"
#include "util/error_response.h"
#include "util/json_value.h"

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
				UpdateItem(endpointAuthHelper, Json::Value::FromText(req.body));
                resp = crow::response(200);
            }
            catch (std::exception& e) {
                resp = ErrorResponse::ValidationError(e.what());
            }
            resp.end();
        }

        class SetupRouting : public RoutingBase {
        public:
            void AddRoute(WebApp* webApp) override {
                CROW_ROUTE(webApp->GetApp(), "/api/update_item")
                    .methods(crow::HTTPMethod::POST)(
                        [=](const crow::request& req, crow::response& resp) {
                            HandlePost(webApp, req, resp);
                        });
            }
        } g_setupRouting;

    }  // namespace

    void UpdateItem(
        EndpointAuthHelper& endpointAuthHelper,
        const Json::Value& message) {
        endpointAuthHelper.GetTransactionProvider()->RunInTransaction([&](Transaction& transaction) {
			if (!message.HasChildren()) {
				throw std::invalid_argument("Invalid JSON object.");
			}
            const Json::Value* tableNameValue = nullptr;
            const Json::Value* valueValue = nullptr;
            const Json::Value* columnNameValue = nullptr;
            const Json::Value* columnValueValue = nullptr;
            if(!message.HasChild("table_name", &tableNameValue)) {
                throw std::invalid_argument("table_name not found.");
            }
            if (!message.HasChild("value", &valueValue)) {
                throw std::invalid_argument("value not found.");
            }
            if (!message.HasChild("column_name", &columnNameValue)) {
                throw std::invalid_argument("column_name not found.");
            }
            if (!message.HasChild("column_value", &columnValueValue)) {
                throw std::invalid_argument("column_value not found.");
            }
            if(!tableNameValue->Is<std::string>() ||
               !columnNameValue->Is<std::string>() ||
               !columnValueValue->Is<std::string>()) {
                throw std::invalid_argument("Invalid message types.");
            }
			std::string tableName = tableNameValue->Get<std::string>();
			std::string columnName = columnNameValue->Get<std::string>();
			std::string columnValue = columnValueValue->Get<std::string>();
            if (!endpointAuthHelper.IsTableAllowed(transaction, tableName)) {
                std::stringstream ss;
                ss << "Table(" << tableName << ") is not an allowed table.";
                throw std::invalid_argument(ss.str());
            }

            DatabaseRESTHelper restHelper(endpointAuthHelper.GetDatabaseHelper());
            restHelper.UpdateTableValue(
                transaction,
                endpointAuthHelper.GetDatabaseInfo(),
                tableName,
                columnName,
                columnValue,
                *valueValue);
            });
    }

}  // namespace Endpoints