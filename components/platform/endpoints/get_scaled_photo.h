#pragma once

#include <crow.h>

#include "endpoints/endpoint_auth_helper.h"

namespace Endpoints {

// GET /api/get_scaled_photo/<string>/<int>/<int>/<int>
// Returns a scaled version of the photo for the specified table item.
// Dimensions are clamped to max public scaled dimensions.
// Sets Content-Type and Cache-Control headers.
//
// Phase 3.7 of the security review: previously a public, unauthenticated
// endpoint that also created a fresh DB connection per request (a
// connection-exhaustion DoS vector). Now it requires login + that the
// table is in the user's allowed list, and uses the shared transaction
// provider just like every other endpoint. For truly public images the
// dedicated /api/home_page_photos endpoint stays unauthenticated.
void GetScaledPhoto(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    std::string_view tableName,
    int64_t tableItemId,
    int width,
    int height);

}  // namespace Endpoints
