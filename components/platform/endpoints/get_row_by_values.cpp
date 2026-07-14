#include "get_row_by_values.h"

#include "endpoint_auth_helper.h"
#include "sql_util/json/database_rest_helper.h"
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
                Json::Value result = GetRowByValues(
                    endpointAuthHelper, Json::Value::FromText(req.body));
                resp.code = 200;
                resp.set_header("Content-Type", "application/json");
                resp.write(result.ToString());
            }
            catch (std::exception& e) {
                resp = ErrorResponse::ValidationError(e.what());
            }
            resp.end();
        }

        class SetupRouting : public RoutingBase {
        public:
            void AddRoute(WebApp* webApp) override {
                CROW_ROUTE(webApp->GetApp(), "/api/get_row_by_values")
                    .methods(crow::HTTPMethod::POST)(
                        [=](const crow::request& req, crow::response& resp) {
                            HandlePost(webApp, req, resp);
                        });
            }
        } g_setupRouting;

    }  // namespace

    Json::Value GetRowByValues(
        EndpointAuthHelper& endpointAuthHelper,
        const Json::Value& message) {
        Json::Value result;
        endpointAuthHelper.GetTransactionProvider()->RunInTransaction([&](Transaction& transaction) {
            if (!message.HasChildren()) {
                throw std::invalid_argument("Invalid JSON object.");
            }

            const Json::Value* tableNameValue = nullptr;
            if (!message.HasChild("table_name", &tableNameValue)) {
                throw std::invalid_argument("table_name not found.");
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

            const Json::Value* filterPairsValue = nullptr;
            if (!message.HasChild("filter_pairs", &filterPairsValue)) {
                throw std::invalid_argument("filter_pairs not found.");
            }
            if (!filterPairsValue->IsArray()) {
                throw std::invalid_argument("filter_pairs must be an array.");
            }
            if (filterPairsValue->GetArray().empty()) {
                throw std::invalid_argument("filter_pairs must not be empty.");
            }

            KeyValueTable lookupValues;
            for (const auto& pair : filterPairsValue->GetArray()) {
                const Json::Value* pairColumnName = nullptr;
                const Json::Value* pairColumnValue = nullptr;
                if (!pair.HasChild("column_name", &pairColumnName)) {
                    throw std::invalid_argument(
                        "filter_pairs entry missing column_name.");
                }
                if (!pair.HasChild("column_value", &pairColumnValue)) {
                    throw std::invalid_argument(
                        "filter_pairs entry missing column_value.");
                }
                lookupValues[pairColumnName->Get<std::string>()] =
                    pairColumnValue->Get<std::string>();
            }

            // Phase 3.8: pass the column-redaction set so credential
            // material is stripped at the JSON boundary.
            DatabaseRESTHelper restHelper(
                endpointAuthHelper.GetDatabaseHelper(),
                endpointAuthHelper.GetColumnRedactions());
            result = restHelper.GetRowByValues(
                transaction, tableName, lookupValues);
            });
        return result;
    }

}  // namespace Endpoints
