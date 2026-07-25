#pragma once

#include <crow.h>

#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// Uses the logged-in user (session) like set_user_info.
// The JSON body must contain: old_password, new_password.
void UpdateUserPassword(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    const Json::Value& message);

} // namespace Endpoints