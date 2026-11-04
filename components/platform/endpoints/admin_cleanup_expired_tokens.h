#pragma once

#include <crow.h>

#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// POST /api/admin/cleanup_expired_tokens (H13) — deletes expired device
// tokens and email verifications (Auth::TokenCleanupHelper). Gated on
// run_scheduled_jobs; the body is ignored. Responds
// {"device_tokens_deleted": n, "email_verifications_deleted": n}.
Json::Value PostAdminCleanupExpiredTokens(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

}  // namespace Endpoints
