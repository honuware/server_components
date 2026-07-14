#pragma once

#include <string>
#include <string_view>

#include <crow.h>

#include "sql_util/database_access/transaction_provider.h"
#include "util/json_value.h"

namespace Endpoints {

// Build version reported in the health response. Reads the env var
// KNOTTYYOGA_VERSION at every call (cheap) and falls back to "unknown" when
// unset or empty. Operators set this on the EC2 to pin which artifact is
// live (e.g., a git short-sha or release tag).
std::string GetBuildVersion();

// Pure: builds the JSON body for the health response. Exposed so unit tests
// can verify the shape without standing up a database.
Json::Value BuildHealthResponse(bool dbOk, std::string_view version);

// Probes the database by running `SELECT 1` inside a fresh transaction.
// Returns true on success, false if the provider is null or the probe
// throws for any reason (connection failure, server down, etc.).
bool ProbeDatabase(TransactionProviderPtr transactionProvider);

// HTTP handler for `GET /api/health`. Sets `resp.code` to 200 when the DB
// probe succeeds, 503 otherwise. Always returns a JSON body containing
// `status`, `db`, and `version`.
//
// Unauthenticated by design: CloudFront and AWS Synthetics probes hit this
// without cookies. The CloudFrontOriginGuard middleware (Phase 1.7) lets
// `/api/health` through specifically so health checks aren't blocked.
Json::Value GetHealth(
    TransactionProviderPtr transactionProvider,
    const crow::request& req,
    crow::response& resp);

}  // namespace Endpoints
