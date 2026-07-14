#include "role_permissions.h"

#include <stdexcept>

#include "db_schema/role_permissions.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

RolePermissions::RolePermissions(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

int64_t RolePermissions::AddRolePermission(
    Transaction& transaction,
    int64_t roleId,
    int64_t permissionId) {
    KeyValueTable kv = {
        { std::string(DbSchema::kRolePermissionsRoleId), StringFromInt(roleId) },
        { std::string(DbSchema::kRolePermissionsPermissionId), StringFromInt(permissionId) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        databaseHelper_,
        DbSchema::kRolePermissionsTable,
        kv);
}

KeyValueTable RolePermissions::GetRolePermission(Transaction& transaction, int64_t id) const {
    KeyValueTable row = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kRolePermissionsTable,
        DbSchema::kRolePermissionsId,
        StringFromInt(id));
    if (row.empty()) {
        throw std::runtime_error("RolePermissions::GetRolePermission - not found");
    }
    return row;
}

KeyValueTableArray RolePermissions::GetRolePermissionsForRole(Transaction& transaction, int64_t roleId) const {
    KeyValueTable filter = {
        { std::string(DbSchema::kRolePermissionsRoleId), StringFromInt(roleId) }
    };
    return DbCrud::GetRowsByValuesWithOrderBy(
        transaction,
        databaseHelper_,
        DbSchema::kRolePermissionsTable,
        filter,
        DbSchema::kRolePermissionsId,
        true);
}

KeyValueTableArray RolePermissions::GetRolePermissionsForPermission(Transaction& transaction, int64_t permissionId) const {
    KeyValueTable filter = {
        { std::string(DbSchema::kRolePermissionsPermissionId), StringFromInt(permissionId) }
    };
    return DbCrud::GetRowsByValuesWithOrderBy(
        transaction,
        databaseHelper_,
        DbSchema::kRolePermissionsTable,
        filter,
        DbSchema::kRolePermissionsId,
        true);
}

KeyValueTableArray RolePermissions::GetRolePermissions(Transaction& transaction) const {
    return DbCrud::GetTableRows(transaction, databaseHelper_, DbSchema::kRolePermissionsTable);
}

void RolePermissions::DeleteRolePermission(Transaction& transaction, int64_t id) {
    // No-op delete if not found; should not throw
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kRolePermissionsTable,
        DbSchema::kRolePermissionsId,
        StringFromInt(id));
}

} // namespace TableHelpers