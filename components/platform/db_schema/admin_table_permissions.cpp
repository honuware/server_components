#include "admin_table_permissions.h"

namespace DbSchema {

    void MakeAdminTablePermissionsTable(DatabaseInfo databaseInfo) {
        databaseInfo.AddTable(kAdminTablePermissionsTable);
        databaseInfo.AddColumnPrimaryKey(
            kAdminTablePermissionsTable,
            kAdminTablePermissionsId,
            DB_TYPE_BIGSERIAL);
        databaseInfo.AddColumnSimple(
            kAdminTablePermissionsTable,
            kAdminTablePermissionsTableName,
            DB_TYPE_STRING);
        databaseInfo.AddColumnForeignKeyRef(
            kPermissionsTable,
            kPermissionsId,
            kAdminTablePermissionsTable,
            kAdminTablePermissionsPermissionId);
    }

}  // namespace DbSchema
