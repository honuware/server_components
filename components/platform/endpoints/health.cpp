#include "health.h"

#include <cstdlib>
#include <stdexcept>

#include "web_app.h"
#include "util/error_response.h"

namespace Endpoints {
namespace {

void HandleGet(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        Json::Value result = GetHealth(
            webApp->GetTransactionProvider(), req, resp);
        resp.set_header("Content-Type", "application/json");
        resp.write(result.ToString());
    }
    catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/health")
            .methods(crow::HTTPMethod::GET)(
                [=](const crow::request& req, crow::response& resp) {
                    HandleGet(webApp, req, resp);
                });
    }
} g_setupRouting;

}  // namespace

std::string GetBuildVersion() {
    const char* val = std::getenv("KNOTTYYOGA_VERSION");
    if (val == nullptr || val[0] == '\0') return "unknown";
    return std::string(val);
}

Json::Value BuildHealthResponse(bool dbOk, std::string_view version) {
    return Json::Value(Json::JsonObject{
        {"status",  Json::Value(dbOk ? "ok" : "fail")},
        {"db",      Json::Value(dbOk ? "ok" : "fail")},
        {"version", Json::Value(std::string(version))}
    });
}

bool ProbeDatabase(TransactionProviderPtr transactionProvider) {
    if (!transactionProvider) return false;
    try {
        transactionProvider->RunInTransaction([](Transaction& transaction) {
            transaction.RunSqlStatementReturningOneValue("SELECT 1");
        });
        return true;
    }
    catch (...) {
        return false;
    }
}

Json::Value GetHealth(
    TransactionProviderPtr transactionProvider,
    const crow::request&,
    crow::response& resp) {
    bool dbOk = ProbeDatabase(transactionProvider);
    resp.code = dbOk ? 200 : 503;
    return BuildHealthResponse(dbOk, GetBuildVersion());
}

}  // namespace Endpoints
