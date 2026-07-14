#include "get_rows_by_column.h"

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
            const std::string& tableName,
            const std::string& columnName,
            bool asc,
            int pageSize,
            int page) {
            try {
                EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
                endpointAuthHelper.Initialize();
                Json::Value result = GetRowsByColumn(
                    endpointAuthHelper, tableName, columnName, asc, pageSize, page);
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
                CROW_ROUTE(
                    webApp->GetApp(),
                    "/api/get_rows_by_column/<string>/<string>/<int>/<int>/<int>")(
                    [=](
                        const crow::request& req,
                        crow::response& resp,
                        const std::string& tableName,
                        const std::string& columnName,
                        int asc,
                        int pageSize,
                        int page) {
                        HandlePost(
                            webApp,
                            req,
                            resp,
                            URLDecode(tableName),
                            URLDecode(columnName),
                            asc != 0,
                            pageSize,
                            page);
                    });
            }
        } g_setupRouting;

    }  // namespace

    Json::Value GetRowsByColumn(
        EndpointAuthHelper& endpointAuthHelper,
        const std::string& tableName,
        const std::string& columnName,
        bool asc,
        int pageSize,
        int page) {
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
            result = restHelper.GetRowsByColumnWithCount(
                transaction, tableName, columnName, asc, pageSize, page);
            });
        return result;
    }

}  // namespace Endpoints