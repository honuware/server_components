#include "get_fk_options.h"

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
                Json::Value result = GetFkOptions(
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
                CROW_ROUTE(webApp->GetApp(), "/api/get_fk_options")
                    .methods(crow::HTTPMethod::POST)(
                        [=](const crow::request& req, crow::response& resp) {
                            HandlePost(webApp, req, resp);
                        });
            }
        } g_setupRouting;

    }  // namespace

    Json::Value GetFkOptions(
        EndpointAuthHelper& endpointAuthHelper,
        const Json::Value& message) {
        Json::Value result;
        endpointAuthHelper.GetTransactionProvider()->RunInTransaction([&](Transaction& transaction) {
            if (!message.HasChildren()) {
                throw std::invalid_argument("Invalid JSON object.");
            }

            const Json::Value* tableNameValue = nullptr;
            const Json::Value* searchTextValue = nullptr;
            const Json::Value* pageSizeValue = nullptr;

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

            std::string searchText;
            if (message.HasChild("search_text", &searchTextValue) &&
                searchTextValue->Is<std::string>()) {
                searchText = searchTextValue->Get<std::string>();
            }

            int pageSize = 50;
            if (message.HasChild("page_size", &pageSizeValue)) {
                pageSize = static_cast<int>(pageSizeValue->Get<int64_t>());
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
            result = restHelper.GetFkOptions(
                transaction, tableName, primaryKeyColumn, displayTemplate,
                searchText, pageSize);
        });
        return result;
    }

}  // namespace Endpoints
