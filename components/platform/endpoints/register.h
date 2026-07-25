#pragma once

#include <crow.h>

#include "endpoints/endpoint_auth_helper.h"
#include "util/json_value.h"

namespace Endpoints {

// POST /api/register with JSON body:
//   { "first_name": "...", "last_name": "...", "email": "...", "password": "..." }
// Phase 3.1 of the security review: the previous URL-path form put the
// password in the URL.
void Register(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    const Json::Value& message);

}  // namespace Endpoints
