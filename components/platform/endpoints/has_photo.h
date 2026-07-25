#pragma once

#include <crow.h>
#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// GET /api/has_photo/<string>/<int>
// Returns whether the specified table item has an associated photo.
// Requires authentication.
Json::Value GetHasPhoto(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    std::string_view tableName,
    int64_t tableItemId);

}  // namespace Endpoints
