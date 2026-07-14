#pragma once

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class AdminTablePermissions {
public:
    explicit AdminTablePermissions(DatabaseHelper databaseHelper);

    int64_t AddAdminTablePermission(
        Transaction& transaction,
        std::string_view tableName,
        int64_t permissionId);

    KeyValueTableArray GetPermissionsForTable(
        Transaction& transaction,
        std::string_view tableName) const;

    StringArray GetTablesForPermissionId(
        Transaction& transaction,
        int64_t permissionId) const;

    KeyValueTableArray GetAllAdminTablePermissions(
        Transaction& transaction) const;

    void DeleteAdminTablePermission(
        Transaction& transaction,
        int64_t id);

private:
    DatabaseHelper databaseHelper_;
};

} // namespace TableHelpers
