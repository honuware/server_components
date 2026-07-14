#include "get_table_rows.h"

#include "endpoint_auth_helper.h"
#include "sql_util/json/database_rest_helper.h"
#include "sql_util/database_access/database_metadata.h"
#include "util/error_response.h"

namespace Endpoints {

    namespace {

        void HandlePost(
            WebApp* webApp,
            const crow::request& req,
            crow::response& resp,
            const std::string& tableName) {
            try {
                EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
                endpointAuthHelper.Initialize();
                Json::Value result = GetTableRows(endpointAuthHelper, tableName);
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
                CROW_ROUTE(webApp->GetApp(), "/api/get_table_rows/<string>")(
                    [=](
                        const crow::request& req,
                        crow::response& resp,
                        const std::string& tableName) {
                        HandlePost(webApp, req, resp, URLDecode(tableName));
                    });
            }
        } g_setupRouting;

    }  // namespace

    Json::Value GetTableRows(
        EndpointAuthHelper& endpointAuthHelper,
        const std::string& tableName) {
        Json::Value result;
        endpointAuthHelper.GetTransactionProvider()->RunInTransaction([&](Transaction& transaction) {
            if (!endpointAuthHelper.IsTableAllowed(transaction, tableName)) {
                std::stringstream ss;
                ss << "Table(" << tableName << ") is not an allowed table.";
                throw std::invalid_argument(ss.str());
            }
            // Phase 3.8: pass the column-redaction set so credential
            // material is stripped at the JSON boundary.
            DatabaseRESTHelper restHelper(
                endpointAuthHelper.GetDatabaseHelper(),
                endpointAuthHelper.GetColumnRedactions());
            result = restHelper.GetTableValues(transaction, tableName);
            });
        return result;
    }

}  // namespace Endpoints