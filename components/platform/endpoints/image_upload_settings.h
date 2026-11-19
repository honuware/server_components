#pragma once

#include <crow.h>
#include "util/json_value.h"

class EndpointAuthHelper;

// The image upload limit (CommunityFinder CM3 Phase 2 §13.1, OQ13 / OQ21):
// the largest image anyone may upload, `image_max_upload_bytes`. 10 MB by
// default; an admin may set 1 to 50 MB. Its default, range and storage are in
// business_logic/images/image_upload_limit.h.
//
// These routes only read and set it. The upload routes (upload_photo,
// upload_user_photo) keep checking it themselves and stay the last word; the
// signed-in read is so the upload control can say a file is too big before
// sending it.
namespace Endpoints {

// GET /api/image_upload_limit — any signed-in session (401 otherwise): the
// people who upload are curators, submitters and anyone changing their own
// photo, not only admins.
//
// 200: { "max_upload_bytes": 10485760 }
Json::Value GetImageUploadLimit(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

// GET /api/admin/image_upload_settings — admin_portal (401 / 403): the limit
// and the range a settings card offers.
//
// 200: { "max_upload_bytes": 10485760, "default_bytes": 10485760,
//        "minimum_bytes": 1048576, "maximum_bytes": 52428800 }
Json::Value GetImageUploadSettings(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

// PUT /api/admin/image_upload_settings — admin_portal (401 / 403).
//
// Body: { "max_upload_bytes": <whole number of bytes> }. A missing or
// non-integer value, or one outside 1–50 MB, is a 400 validation error whose
// detail says why, with field "max_upload_bytes", and nothing is stored.
// 200: the same body as the GET, with the new limit.
Json::Value PutImageUploadSettings(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

}  // namespace Endpoints
