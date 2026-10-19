#pragma once

#include <crow.h>

#include "endpoints/endpoint_auth_helper.h"
#include "util/json_value.h"

namespace Endpoints {

// POST /api/reset_password with JSON body
//   { "email": "...", "secret": "...", "new_password": "..." }
// Finishing website Phase 11.4. The reset mail's link opens the SPA, which
// posts here (the secret never travels in an API URL). 200 when the password
// was set; otherwise ONE generic 400, "reset link is invalid or expired",
// whatever was wrong with the link. Failures are capped per IP, as /api/verify
// does, with the generic 429.
void ResetPassword(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    const Json::Value& message);

}  // namespace Endpoints
