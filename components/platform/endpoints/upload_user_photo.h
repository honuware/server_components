#pragma once

#include <crow.h>
#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// POST /api/upload_user_photo/<string>
// Uploads a photo for the logged-in user's people record.
// Path param: imageType (jpeg or png).
// Request body: raw image bytes.
// Requires authentication (any logged-in user).
Json::Value PostUploadUserPhoto(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    std::string_view imageType);

}  // namespace Endpoints
