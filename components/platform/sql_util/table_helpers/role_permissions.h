#pragma once

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class RolePermissions {
public:
    explicit RolePermissions(DatabaseHelper databaseHelper);

    int64_t AddRolePermission(Transaction& transaction, int64_t roleId, int64_t permissionId);

    KeyValueTable GetRolePermission(Transaction& transaction, int64_t id) const;

    KeyValueTableArray GetRolePermissionsForRole(Transaction& transaction, int64_t roleId) const;
    KeyValueTableArray GetRolePermissionsForPermission(Transaction& transaction, int64_t permissionId) const;

    KeyValueTableArray GetRolePermissions(Transaction& transaction) const;

    void DeleteRolePermission(Transaction& transaction, int64_t id);

private:
    DatabaseHelper databaseHelper_;
};

} // namespace TableHelpers