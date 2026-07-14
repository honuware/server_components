#include "resolve_fk_display.h"

#include "endpoint_auth_helper.h"
#include "sql_util/json/database_rest_helper.h"
#include "sql_util/table_helpers/admin_table_display_template.h"
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
                Json::Value result = ResolveFkDisplay(
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
                CROW_ROUTE(webApp->GetApp(), "/api/resolve_fk_display")
                    .methods(crow::HTTPMethod::POST)(
                        [=](const crow::request& req, crow::response& resp) {
                            HandlePost(webApp, req, resp);
                        });
            }
        } g_setupRouting;

    }  // namespace

    Json::Value ResolveFkDisplay(
        EndpointAuthHelper& endpointAuthHelper,
        const Json::Value& message) {
        Json::Value result;
        endpointAuthHelper.GetTransactionProvider()->RunInTransaction([&](Transaction& transaction) {
            if (!message.HasChildren()) {
                throw std::invalid_argument("Invalid JSON object.");
            }

            const Json::Value* tableNameValue = nullptr;
            const Json::Value* valuesValue = nullptr;

            if (!message.HasChild("parent_table_name", &tableNameValue)) {
                throw std::invalid_argument("parent_table_name not found.");
            }
            if (!tableNameValue->Is<std::string>()) {
                throw std::invalid_argument("parent_table_name must be a string.");
            }

            std::string tableName = tableNameValue->Get<std::string>();

            if (!endpointAuthHelper.IsTableAllowed(transaction, tableName)) {
                std::stringstream ss;
                ss << "Table(" << tableName << ") is not an allowed table.";
                throw std::invalid_argument(ss.str());
            }

            if (!message.HasChild("values", &valuesValue)) {
                throw std::invalid_argument("values not found.");
            }
            if (!valuesValue->IsArray()) {
                throw std::invalid_argument("values must be an array.");
            }

            StringArray values;
            for (const auto& item : valuesValue->GetArray()) {
                values.push_back(item.Get<std::string>());
            }

            // Get primary key column from schema
            const auto& tableInfo =
                endpointAuthHelper.GetDatabaseInfo().GetTableInfo(tableName);
            std::string primaryKeyColumn = tableInfo.GetPrimaryKey();

            // Get display template
            TableHelpers::AdminTableDisplayTemplate displayTemplateHelper(
                endpointAuthHelper.GetDatabaseHelper());
            std::string displayTemplate =
                displayTemplateHelper.GetDisplayTemplate(transaction, tableName);

            DatabaseRESTHelper restHelper(endpointAuthHelper.GetDatabaseHelper());
            result = restHelper.ResolveFkDisplay(
                transaction, tableName, primaryKeyColumn, displayTemplate, values);
        });
        return result;
    }

}  // namespace Endpoints
