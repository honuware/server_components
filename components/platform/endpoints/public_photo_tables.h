#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"

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
    // H15 — decides, row by row, whether a listed table's photo is public.
    using RowRule = std::function<bool(
        const DatabaseHelper& databaseHelper, Transaction& transaction,
        std::string_view tableName, int64_t tableItemId)>;

    std::vector<std::string> tables;

    // Optional (H15). When set, a listed table's photo is public only for the rows
    // the rule accepts; a row it refuses falls back to the signed-in read (login +
    // IsTableAllowed, `private` cache headers), so whoever administers the table
    // still sees it. Unset, every row of a listed table is public. Tables that
    // aren't listed never reach it. communityfinder keeps a pending event's photo
    // private until a curator approves the event.
    RowRule isRowPublic;

    bool Contains(std::string_view tableName) const {
        return std::find(tables.begin(), tables.end(), tableName) != tables.end();
    }

    // Is this row's photo public: its table listed, and the rule (if any) agreeing?
    bool IsRowPublic(const DatabaseHelper& databaseHelper, Transaction& transaction,
                     std::string_view tableName, int64_t tableItemId) const {
        if (!Contains(tableName)) return false;
        return !isRowPublic || isRowPublic(databaseHelper, transaction, tableName, tableItemId);
    }
};

}  // namespace Endpoints
