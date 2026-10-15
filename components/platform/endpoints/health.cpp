#include "health.h"

#include <cstdlib>
#include <optional>
#include <stdexcept>

#include "web_app.h"
#include "business_logic/tenancy/tenant_header.h"
#include "business_logic/tenancy/tenant_resolver.h"
#include "business_logic/tenancy/tenant_resources.h"
#include "db_schema/tenants.h"
#include "util/env.h"
#include "util/error_response.h"
#include "util/logging.h"

namespace Endpoints {
namespace {

constexpr std::string_view kGlobalTargetName = "global";

void HandleGet(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        Json::Value result = GetHealth(
            CollectHealthProbeTargets(*webApp, req), req, resp);
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
    // Canonical HONUWARE_VERSION, with the legacy KNOTTYYOGA_VERSION as a
    // fallback (componentization Phase 1.1 rename convention).
    const char* val =
        Util::GetEnvWithFallback("HONUWARE_VERSION", "KNOTTYYOGA_VERSION");
    if (val == nullptr || val[0] == '\0') return "unknown";
    return std::string(val);
}

Json::Value BuildHealthResponse(
    bool dbOk, std::string_view version, const std::vector<std::string>& failed) {
    Json::JsonObject body{
        {"status",  Json::Value(dbOk ? "ok" : "fail")},
        {"db",      Json::Value(dbOk ? "ok" : "fail")},
        {"version", Json::Value(std::string(version))}
    };
    if (!failed.empty()) {
        Json::JsonArray names;
        for (const std::string& name : failed) {
            names.push_back(Json::Value(name));
        }
        body["failed"] = Json::Value(names);
    }
    return Json::Value(body);
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

std::vector<HealthProbeTarget> CollectHealthProbeTargets(
    WebApp& webApp, const crow::request& req) {
    std::vector<HealthProbeTarget> targets;
    targets.push_back({std::string(kGlobalTargetName), webApp.GetTransactionProvider()});

    std::shared_ptr<Tenancy::TenantResourceRegistry> registry =
        webApp.GetTenantResourceRegistry();
    if (!registry) {
        return targets;  // tenancy inert: the global provider is all there is
    }

    // This request's tenant, resolved the way EndpointAuthHelper::ResolveTenant
    // does. Resolving builds the tenant's pool if no request has reached it yet,
    // so a health check straight after a restart probes the pool too instead of
    // reporting ok on the global connection alone.
    std::shared_ptr<Tenancy::TenantResolver> resolver = webApp.GetTenantResolver();
    if (resolver) {
        std::string siteKey(
            req.get_header_value(std::string(Tenancy::kSiteHeaderName)));
        std::optional<Tenancy::TenantContext> resolved = resolver->Resolve(siteKey);
        if (resolved.has_value() &&
            resolved->status != std::string(DbSchema::kTenantStatusSuspended)) {
            registry->GetOrCreate(*resolved);
        }
    }

    for (const Tenancy::TenantResourcesPtr& resources : registry->GetAll()) {
        if (resources && resources->transactionProvider) {
            targets.push_back(
                {resources->context.databaseName, resources->transactionProvider});
        }
    }
    return targets;
}

Json::Value GetHealth(
    const std::vector<HealthProbeTarget>& targets,
    const crow::request&,
    crow::response& resp) {
    std::vector<std::string> failed;
    for (const HealthProbeTarget& target : targets) {
        if (!ProbeDatabase(target.transactionProvider)) {
            failed.push_back(target.name);
        }
    }
    // No targets at all means nothing could be checked, which is not healthy.
    const bool dbOk = !targets.empty() && failed.empty();
    if (!dbOk) {
        LogError() << "[health] event=probe_failed failed=" << failed.size()
                   << " of=" << targets.size() << "\n";
    }
    resp.code = dbOk ? 200 : 503;
    return BuildHealthResponse(dbOk, GetBuildVersion(), failed);
}

Json::Value GetHealth(
    TransactionProviderPtr transactionProvider,
    const crow::request& req,
    crow::response& resp) {
    return GetHealth(
        std::vector<HealthProbeTarget>{
            {std::string(kGlobalTargetName), std::move(transactionProvider)}},
        req, resp);
}

}  // namespace Endpoints
