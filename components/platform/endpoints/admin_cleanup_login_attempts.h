#pragma once

#include <crow.h>

#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// POST /api/admin/cleanup_login_attempts (H13) — deletes login_attempts rows
// older than auth_login_attempts_retention_in_micros (30 days when unset).
// Gated on run_scheduled_jobs; the body is ignored. Responds
// {"login_attempts_deleted": n}.
Json::Value PostAdminCleanupLoginAttempts(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

}  // namespace Endpoints
