#pragma once

#include <crow.h>

class EndpointAuthHelper;

namespace Endpoints {

// Always returns 200 (Logout should never fail). Never exposes internal details.
void Logout(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

} // namespace Endpoints