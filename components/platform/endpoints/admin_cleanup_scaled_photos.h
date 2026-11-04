#pragma once

#include <crow.h>

#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// POST /api/admin/cleanup_scaled_photos (H13) — deletes cached scaled photo
// copies not used within scaled_photo_max_age_us (30 days when unset), stored
// bytes included; they regenerate on demand. Gated on run_scheduled_jobs; the
// body is ignored. Responds {"scaled_photos_deleted": n}.
Json::Value PostAdminCleanupScaledPhotos(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

}  // namespace Endpoints
