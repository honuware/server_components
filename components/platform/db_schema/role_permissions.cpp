#include "role_permissions.h"

namespace DbSchema {

    void MakeRolePermissionsTable(DatabaseInfo databaseInfo) {
        databaseInfo.AddTable(kRolePermissionsTable);
        databaseInfo.AddColumnPrimaryKey(
            kRolePermissionsTable,
            kRolePermissionsId,
            DB_TYPE_BIGSERIAL);
        // Foreign keys
        databaseInfo.AddColumnForeignKeyRef(
            kRolesTable,
            kRolesId,
            kRolePermissionsTable,
            kRolePermissionsRoleId);
        databaseInfo.AddColumnForeignKeyRef(
            kPermissionsTable,
            kPermissionsId,
            kRolePermissionsTable,
            kRolePermissionsPermissionId);
        // A given (role, permission) pair is meaningful at most once. Phase
        // 1.5 of the security review.
        databaseInfo.AddUniqueConstraint(
            kRolePermissionsTable,
            kRolePermissionsRoleId,
            kRolePermissionsPermissionId);
    }

}  // namespace DbSchema