#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

// CRUD for the images a theme carries (Tenant Theming Phase 9). Addressed by
// NAME rather than id, because that is how a theme file refers to them and how
// the serving route addresses them.
//
// See db_schema/site_assets.h for why these are not photo_instances rows.
class SiteAssets {
public:
    SiteAssets(DatabaseHelper databaseHelper);
    SiteAssets(const SiteAssets&) = default;
    SiteAssets& operator=(const SiteAssets&) = default;
    ~SiteAssets() = default;

    // Insert or replace by name. Importing a theme twice must not accumulate
    // rows, and the second import's bytes are the ones that should win.
    int64_t PutAsset(
        Transaction& transaction,
        std::string_view name,
        std::string_view type,
        std::string_view bytes);

    // Metadata only — never drags the bytes along. The manage pages list assets
    // and must not pull megabytes to do it.
    KeyValueTable GetAssetByName(
        Transaction& transaction, std::string_view name) const;
    KeyValueTableArray GetAllAssets(Transaction& transaction) const;

    // Fetched on its own, for the serving route.
    std::string GetAssetBytes(Transaction& transaction, std::string_view name) const;

    void DeleteAssetByName(Transaction& transaction, std::string_view name);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
