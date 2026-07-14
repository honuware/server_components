#pragma once

#include "sql_util/schema/database_info.h"
#include "permissions.h"

namespace DbSchema {

    constexpr std::string_view kAdminTablePermissionsTable = "admin_table_permissions";
    constexpr std::string_view kAdminTablePermissionsId = "id";
    constexpr std::string_view kAdminTablePermissionsTableName = "table_name";
    constexpr std::string_view kAdminTablePermissionsPermissionId = "permission_id";

    void MakeAdminTablePermissionsTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema
