#include "tenants.h"

#include <string>

#include "db_schema/tenants.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {
namespace {

TenantRow RowFromKeyValueTable(const KeyValueTable& row) {
    TenantRow result;
    result.id = std::stoll(row.at(std::string(DbSchema::kTenantsId)));
    result.siteKey = row.at(std::string(DbSchema::kTenantsSiteKey));
    result.databaseName = row.at(std::string(DbSchema::kTenantsDatabaseName));
    result.displayName = row.at(std::string(DbSchema::kTenantsDisplayName));
    result.status = row.at(std::string(DbSchema::kTenantsStatus));
    result.maxConnections =
        std::stoll(row.at(std::string(DbSchema::kTenantsMaxConnections)));
    return result;
}

}  // namespace

Tenants::Tenants(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

int64_t Tenants::Insert(Transaction& transaction, const TenantRow& row) {
    KeyValueTable kv = {
        {std::string(DbSchema::kTenantsSiteKey), row.siteKey},
        {std::string(DbSchema::kTenantsDatabaseName), row.databaseName},
        {std::string(DbSchema::kTenantsDisplayName), row.displayName},
    };
    // Leave status / max_connections unset so the column defaults apply unless
    // the caller supplied non-default values.
    if (!row.status.empty()) {
        kv[std::string(DbSchema::kTenantsStatus)] = row.status;
    }
    if (row.maxConnections > 0) {
        kv[std::string(DbSchema::kTenantsMaxConnections)] =
            StringFromInt(row.maxConnections);
    }
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction, databaseHelper_, DbSchema::kTenantsTable, kv);
}

std::optional<TenantRow> Tenants::LookupBySiteKey(
    Transaction& transaction, std::string_view siteKey) const {
    KeyValueTable row = DbCrud::LookupRowByValue(
        transaction, databaseHelper_,
        DbSchema::kTenantsTable, DbSchema::kTenantsSiteKey, siteKey);
    if (row.empty()) {
        return std::nullopt;
    }
    return RowFromKeyValueTable(row);
}

std::vector<TenantRow> Tenants::ListActive(Transaction& transaction) const {
    KeyValueTable filter = {
        {std::string(DbSchema::kTenantsStatus),
         std::string(DbSchema::kTenantStatusActive)},
    };
    KeyValueTableArray rows = DbCrud::GetRowsByValuesWithOrderBy(
        transaction, databaseHelper_, DbSchema::kTenantsTable,
        filter, DbSchema::kTenantsId, /*asc=*/true);
    std::vector<TenantRow> result;
    result.reserve(rows.size());
    for (const KeyValueTable& row : rows) {
        result.push_back(RowFromKeyValueTable(row));
    }
    return result;
}

void Tenants::SetStatus(
    Transaction& transaction, std::string_view siteKey, std::string_view status) {
    KeyValueTable kv = {
        {std::string(DbSchema::kTenantsStatus), std::string(status)},
        {std::string(DbSchema::kTenantsUpdatedUs), "now_us()"},
    };
    // now_us() is a SQL expression, not a bound literal — whitelist it.
    StringSet allowedSqlKeywords = {"now_us()"};
    DbCrud::UpdateRow(
        transaction, databaseHelper_, DbSchema::kTenantsTable,
        DbSchema::kTenantsSiteKey, siteKey, kv, allowedSqlKeywords);
}

}  // namespace TableHelpers
