#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace Endpoints {

// The set of tables whose SCALED photos anonymous visitors may read via
// /api/get_scaled_photo — bypassing the login + IsTableAllowed checks and served
// with public (edge-cacheable) headers (H8 extraction seam).
//
// A consuming app registers its public-imagery tables on the WebApp service
// registry — webApp.SetService<Endpoints::PublicPhotoTables>(tables). When none is
// registered, NOTHING is public: every scaled-photo read requires login — the safe
// default for a new consumer. This is deployment-wide config, not per-tenant today.
// knottyyoga registers {home_page_photos, classes, instructors}.
struct PublicPhotoTables {
    std::vector<std::string> tables;

    bool Contains(std::string_view tableName) const {
        return std::find(tables.begin(), tables.end(), tableName) != tables.end();
    }
};

}  // namespace Endpoints
