#pragma once

#include <crow.h>

class EndpointAuthHelper;

namespace Endpoints {

// DELETE /api/delete_photo/<string>/<int>
// Deletes the photo associated with the specified table item.
// Requires admin, or the user can delete their own people photo.
void DeletePhoto(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    std::string_view tableName,
    int64_t tableItemId);

}  // namespace Endpoints
