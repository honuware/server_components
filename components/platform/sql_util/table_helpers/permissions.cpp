#include "permissions.h"

#include <stdexcept>

#include "db_schema/permissions.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

Permissions::Permissions(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

int64_t Permissions::AddPermission(
    Transaction& transaction,
    std::string_view name,
    std::string_view description) {
    KeyValueTable kv = {
        { std::string(DbSchema::kPermissionsName), std::string(name) },
        { std::string(DbSchema::kPermissionsDescription), std::string(description) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        databaseHelper_,
        DbSchema::kPermissionsTable,
        kv);
}

KeyValueTable Permissions::GetPermission(Transaction& transaction, int64_t id) const {
    KeyValueTable row = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kPermissionsTable,
        DbSchema::kPermissionsId,
        StringFromInt(id));
    if (row.empty()) {
        throw std::runtime_error("Permissions::GetPermission(id) - not found");
    }
    return row;
}

KeyValueTable Permissions::GetPermission(Transaction& transaction, std::string_view name) const {
    KeyValueTable row = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kPermissionsTable,
        DbSchema::kPermissionsName,
        name);
    if (row.empty()) {
        throw std::runtime_error("Permissions::GetPermission(name) - not found");
    }
    return row;
}

KeyValueTableArray Permissions::GetPermissions(Transaction& transaction) const {
    return DbCrud::GetTableRows(transaction, databaseHelper_, DbSchema::kPermissionsTable);
}

KeyValueTableArray Permissions::GetPricingEligiblePermissions(
    Transaction& transaction) const {
    KeyValueTableArray result;
    for (const auto& row : GetPermissions(transaction)) {
        auto it = row.find(std::string(DbSchema::kPermissionsIsPricingEligible));
        if (it != row.end()
            && (it->second == "t" || it->second == "true" || it->second == "1")) {
            result.push_back(row);
        }
    }
    return result;
}

void Permissions::SetName(Transaction& transaction, int64_t id, std::string_view name) {
    // Ensure exists
    (void)GetPermission(transaction, id);

    KeyValueTable kv = {
        { std::string(DbSchema::kPermissionsName), std::string(name) }
    };
    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kPermissionsTable,
        DbSchema::kPermissionsId,
        StringFromInt(id),
        kv);
}

void Permissions::SetDescription(Transaction& transaction, int64_t id, std::string_view description) {
    // Ensure exists
    (void)GetPermission(transaction, id);

    KeyValueTable kv = {
        { std::string(DbSchema::kPermissionsDescription), std::string(description) }
    };
    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kPermissionsTable,
        DbSchema::kPermissionsId,
        StringFromInt(id),
        kv);
}

void Permissions::SetPricingEligible(
    Transaction& transaction, int64_t id, bool eligible) {
    // Ensure exists
    (void)GetPermission(transaction, id);

    KeyValueTable kv = {
        { std::string(DbSchema::kPermissionsIsPricingEligible),
          eligible ? "true" : "false" }
    };
    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kPermissionsTable,
        DbSchema::kPermissionsId,
        StringFromInt(id),
        kv);
}

void Permissions::DeletePermission(Transaction& transaction, int64_t id) {
    // This delete should be a no-op if nothing matches; do not throw on not-found.
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kPermissionsTable,
        DbSchema::kPermissionsId,
        StringFromInt(id));
}

} // namespace TableHelpers