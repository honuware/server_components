#pragma once

#include <crow.h>

#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// Uses the logged-in user (session) like get_user_info.
// The JSON body may contain any of: email, first_name, last_name. Missing fields are not an error.
void SetUserInfo(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    const Json::Value& message);

} // namespace Endpoints