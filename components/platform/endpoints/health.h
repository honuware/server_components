#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <crow.h>

#include "sql_util/database_access/transaction_provider.h"
#include "util/json_value.h"

class WebApp;

namespace Endpoints {

// Build version reported in the health response. Reads the env var
// HONUWARE_VERSION at every call (cheap), falling back to the legacy
// KNOTTYYOGA_VERSION and then to "unknown" when unset or empty. Operators set
// this on the EC2 to pin which artifact is live (e.g., a git short-sha or
// release tag).
std::string GetBuildVersion();

// Pure: builds the JSON body for the health response. Exposed so unit tests
// can verify the shape without standing up a database. `failed` names the
// connections whose probe failed and is included only when non-empty, so a
// healthy body is exactly what it has always been.
Json::Value BuildHealthResponse(
    bool dbOk, std::string_view version,
    const std::vector<std::string>& failed = {});

// Probes the database by running `SELECT 1` inside a fresh transaction.
// Returns true on success, false if the provider is null or the probe
// throws for any reason (connection failure, server down, etc.).
bool ProbeDatabase(TransactionProviderPtr transactionProvider);

// One connection the health check probes, and the name a failure reports it
// by ("global", or a tenant's database name).
struct HealthProbeTarget {
    std::string name;
    TransactionProviderPtr transactionProvider;
};

// The connections a request on this server can run on. Health must probe
// THESE, not just the global one: on 10/15/2026 the site's tenant pool was
// dead for hours while the global connection -- the only thing health then
// probed -- answered fine, so health said ok throughout the outage.
//   - the deployment's global provider ("global")
//   - every tenant pool built so far
//   - this request's own tenant, resolved exactly as EndpointAuthHelper does,
//     so a freshly started server's tenant pool is built and probed too
std::vector<HealthProbeTarget> CollectHealthProbeTargets(
    WebApp& webApp, const crow::request& req);

// Probes every target. 200 only when all pass; otherwise 503 with the
// failures named. Always returns a JSON body containing `status`, `db`, and
// `version` (+ `failed` on failure).
Json::Value GetHealth(
    const std::vector<HealthProbeTarget>& targets,
    const crow::request& req,
    crow::response& resp);

// HTTP handler for `GET /api/health` over a single provider, reported as
// "global". Sets `resp.code` to 200 when the DB probe succeeds, 503 otherwise.
//
// Unauthenticated by design: CloudFront and AWS Synthetics probes hit this
// without cookies. The CloudFrontOriginGuard middleware (Phase 1.7) lets
// `/api/health` through specifically so health checks aren't blocked.
Json::Value GetHealth(
    TransactionProviderPtr transactionProvider,
    const crow::request& req,
    crow::response& resp);

}  // namespace Endpoints
