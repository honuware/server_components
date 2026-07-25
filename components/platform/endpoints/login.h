#pragma once

#include <crow.h>

#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// Helper invoked by the route handler (context supplied indirectly).
void Login(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    const Json::Value& message);

} // namespace Endpoints