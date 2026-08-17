#include "site_assets.h"

#include "db_schema/site_assets.h"
#include "sql_util/database_access/bytea.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {
namespace {

// Deliberately does NOT select `bytes` — a listing must not pull the images.
constexpr std::string_view kSqlAllAssets =
    "SELECT site_asset_id, name, type, created_at_us, last_updated_at_us "
    "FROM site_assets ORDER BY name ASC";

constexpr std::string_view kSqlAssetByName =
    "SELECT site_asset_id, name, type, created_at_us, last_updated_at_us "
    "FROM site_assets WHERE name = $1";

constexpr std::string_view kSqlAssetBytes =
    "SELECT bytes FROM site_assets WHERE name = $1";

}  // namespace

SiteAssets::SiteAssets(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

int64_t SiteAssets::PutAsset(
    Transaction& transaction,
    std::string_view name,
    std::string_view type,
    std::string_view bytes) {
    KeyValueTable kv = {
        { std::string(DbSchema::kSiteAssetType), std::string(type) },
        // Binary values cross the KeyValueTable layer as bytea hex text.
        { std::string(DbSchema::kSiteAssetBytes), SqlUtil::ByteaHexEncode(bytes) },
    };
    KeyValueTable existing = GetAssetByName(transaction, name);
    if (!existing.empty()) {
        const int64_t id =
            std::atoll(existing.at(std::string(DbSchema::kSiteAssetId)).c_str());
        DbCrud::UpdateRow(
            transaction, databaseHelper_, DbSchema::kSiteAssets,
            DbSchema::kSiteAssetId, StringFromInt(id), kv);
        return id;
    }
    kv[std::string(DbSchema::kSiteAssetName)] = std::string(name);
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction, databaseHelper_, DbSchema::kSiteAssets, kv);
}

KeyValueTable SiteAssets::GetAssetByName(
    Transaction& transaction, std::string_view name) const {
    return transaction.RunSqlStatementReturningOneRow(
        std::string(kSqlAssetByName), name);
}

KeyValueTableArray SiteAssets::GetAllAssets(Transaction& transaction) const {
    return transaction.RunSqlStatementReturningKeyValueTableArray(
        std::string(kSqlAllAssets));
}

std::string SiteAssets::GetAssetBytes(
    Transaction& transaction, std::string_view name) const {
    // The name comes from a URL slot, which can outlive the row it points at —
    // a theme import that replaced the asset set is exactly how that happens.
    // RunSqlStatementReturningOneValue THROWS on zero rows, so a missing asset
    // would surface as a 500 on a public page rather than a missing image.
    if (GetAssetByName(transaction, name).empty()) {
        return {};
    }
    return SqlUtil::ByteaHexDecode(transaction.RunSqlStatementReturningOneValue(
        std::string(kSqlAssetBytes), name));
}

void SiteAssets::DeleteAssetByName(
    Transaction& transaction, std::string_view name) {
    DbCrud::DeleteRow(
        transaction, databaseHelper_, DbSchema::kSiteAssets,
        DbSchema::kSiteAssetName, name);
}

}  // namespace TableHelpers
