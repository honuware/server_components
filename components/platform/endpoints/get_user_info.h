#pragma once

#include <crow.h>

#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// Returns JSON with user info on success (resp.code = 200). On failure sets resp.code = 401.
// Never exposes internal error details.
Json::Value GetUserInfo(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

} // namespace Endpoints