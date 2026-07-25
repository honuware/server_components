#pragma once

#include <crow.h>
#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// POST /api/upload_photo/<string>/<int>/<string>
// Uploads a photo for the specified table item.
// Path params: tableName, tableItemId, imageType (jpeg or png).
// Request body: raw image bytes.
// Requires admin authentication.
Json::Value PostUploadPhoto(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    std::string_view tableName,
    int64_t tableItemId,
    std::string_view imageType);

}  // namespace Endpoints
