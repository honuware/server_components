#pragma once

#include <crow.h>

class EndpointAuthHelper;

namespace Endpoints {

// Returns true on success; sets resp.code = 200. On failure sets resp.code = 401.
// Never exposes internal error details.
bool Remember(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

} // namespace Endpoints