#pragma once

#include <crow.h>
#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// GET /api/site_font_face/<id>
//
// Serves one uploaded font face's bytes for the resolved tenant. Public and
// unauthenticated by design — a webfont is fetched by the browser's font loader
// with no credentials, and the page it styles is public anyway.
//
// Tenant Theming Phase 4B / D14. The headers are the security-relevant part:
//   * `Content-Type: font/woff2` (or woff/ttf/otf) from the format the MAGIC
//     BYTES said it was at upload time — never the uploader's filename;
//   * `X-Content-Type-Options: nosniff`, so bytes a tenant uploaded can never
//     be reinterpreted as script from our own origin;
//   * a long max-age, because a face is immutable — a new upload is a new id.
//
// Returns void rather than a Json::Value because the body is binary; the
// signature still matches the endpoint anchor pattern in web_app.
void GetSiteFontFace(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    int64_t faceId);

}  // namespace Endpoints
