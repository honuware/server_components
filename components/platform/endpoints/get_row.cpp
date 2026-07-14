#include "get_row.h"
#include "endpoint_auth_helper.h"
#include "sql_util/json/database_rest_helper.h"
#include "util/error_response.h"

namespace Endpoints {

namespace {

void HandlePost(
    WebApp* webApp,
    const crow::request& req,
    crow::response& resp,
    const std::string& tableName,
    const std::string& columnName,
    const std::string& value) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        Json::Value result = GetRow(endpointAuthHelper, tableName, columnName, value);
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
            "/api/get_row/<string>/<string>/<string>")(
            [=](
                const crow::request& req,
                crow::response& resp,
                const std::string& tableName,
                const std::string& columnName,
                const std::string& value) {
                HandlePost(webApp, req, resp, URLDecode(tableName), URLDecode(columnName), value);
            });
    }
} g_setupRouting;

} // namespace

Json::Value GetRow(
    EndpointAuthHelper& endpointAuthHelper,
    const std::string& tableName,
    const std::string& columnName,
    const std::string& value) {
    Json::Value result;
    endpointAuthHelper.GetTransactionProvider()->RunInTransaction([&](Transaction& transaction) {
        if (!endpointAuthHelper.IsTableAllowed(transaction, tableName)) {
            std::stringstream ss;
            ss << "Table(" << tableName << ") is not an allowed table.";
            throw std::invalid_argument(ss.str());
        }
        // Phase 3.8: pass the column-redaction set so credential
        // material (people.password_hash, sessions.uuid, etc.) is
        // stripped from the response at the JSON boundary.
        DatabaseRESTHelper restHelper(
            endpointAuthHelper.GetDatabaseHelper(),
            endpointAuthHelper.GetColumnRedactions());
        result = restHelper.GetRow(transaction, tableName, columnName, value);
        });
    return result;
}

} // namespace Endpoints