#pragma once

#include <crow.h>

#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// POST /api/admin/cleanup_idempotency_keys (H13) — deletes idempotency keys
// past their expiry. Gated on run_scheduled_jobs; the body is ignored.
// Responds {"idempotency_keys_deleted": n}.
Json::Value PostAdminCleanupIdempotencyKeys(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

}  // namespace Endpoints
