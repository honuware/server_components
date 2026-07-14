#include "admin_table_permissions.h"

#include "db_schema/admin_table_permissions.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

AdminTablePermissions::AdminTablePermissions(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

int64_t AdminTablePermissions::AddAdminTablePermission(
    Transaction& transaction,
    std::string_view tableName,
    int64_t permissionId) {
    KeyValueTable kv = {
        { std::string(DbSchema::kAdminTablePermissionsTableName), std::string(tableName) },
        { std::string(DbSchema::kAdminTablePermissionsPermissionId), StringFromInt(permissionId) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        databaseHelper_,
        DbSchema::kAdminTablePermissionsTable,
        kv);
}

KeyValueTableArray AdminTablePermissions::GetPermissionsForTable(
    Transaction& transaction,
    std::string_view tableName) const {
    KeyValueTable filter = {
        { std::string(DbSchema::kAdminTablePermissionsTableName), std::string(tableName) }
    };
    return DbCrud::GetRowsByValuesWithOrderBy(
        transaction,
        databaseHelper_,
        DbSchema::kAdminTablePermissionsTable,
        filter,
        DbSchema::kAdminTablePermissionsId,
        true);
}

StringArray AdminTablePermissions::GetTablesForPermissionId(
    Transaction& transaction,
    int64_t permissionId) const {
    KeyValueTable filter = {
        { std::string(DbSchema::kAdminTablePermissionsPermissionId), StringFromInt(permissionId) }
    };
    KeyValueTableArray rows = DbCrud::GetRowsByValuesWithOrderBy(
        transaction,
        databaseHelper_,
        DbSchema::kAdminTablePermissionsTable,
        filter,
        DbSchema::kAdminTablePermissionsTableName,
        true);
    StringArray tables;
    for (const auto& row : rows) {
        tables.push_back(row.at(std::string(DbSchema::kAdminTablePermissionsTableName)));
    }
    return tables;
}

KeyValueTableArray AdminTablePermissions::GetAllAdminTablePermissions(
    Transaction& transaction) const {
    return DbCrud::GetTableRows(
        transaction,
        databaseHelper_,
        DbSchema::kAdminTablePermissionsTable);
}

void AdminTablePermissions::DeleteAdminTablePermission(
    Transaction& transaction,
    int64_t id) {
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kAdminTablePermissionsTable,
        DbSchema::kAdminTablePermissionsId,
        StringFromInt(id));
}

} // namespace TableHelpers
