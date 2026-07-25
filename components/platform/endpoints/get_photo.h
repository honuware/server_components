#pragma once

#include <crow.h>

class EndpointAuthHelper;

namespace Endpoints {

// GET /api/get_photo/<string>/<int>
// Returns the source photo binary data for the specified table item.
// Sets Content-Type and Cache-Control headers.
// Requires authentication.
void GetPhoto(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    std::string_view tableName,
    int64_t tableItemId);

}  // namespace Endpoints
