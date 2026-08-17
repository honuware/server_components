#pragma once

#include <crow.h>
#include <string>

class EndpointAuthHelper;

namespace Endpoints {

// GET /api/site_asset/<name>
//
// Serves a theme's own image — the logo, favicon or hero a theme bundle carried
// with it (Tenant Theming Phase 9). Public and unauthenticated by design: these
// are the images on the public site's own pages.
//
// The headers are the security-relevant part, and they mirror site_font_face
// for the same reason:
//   * `Content-Type` from the type the MAGIC BYTES said it was at import time —
//     never a filename's extension;
//   * `X-Content-Type-Options: nosniff`, so bytes an admin imported can never be
//     reinterpreted as script served from our own origin;
//   * `no-cache` rather than a long max-age, unlike a font face: a font id is
//     immutable (a new upload is a new id), but an asset is addressed by NAME
//     and importing a different theme replaces `logo.png` in place. A cached
//     copy would be the previous studio's logo — the same failure the
//     site_info cache produced, which is not one to repeat.
//
// Returns void because the body is binary.
void GetSiteAsset(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    const std::string& name);

}  // namespace Endpoints
