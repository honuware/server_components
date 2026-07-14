#pragma once

#include "sql_util/schema/database_info.h"
#include "roles.h"
#include "permissions.h"

namespace DbSchema {

    // Table and column identifiers
    constexpr std::string_view kRolePermissionsTable = "role_permissions";
    constexpr std::string_view kRolePermissionsId = "id";
    constexpr std::string_view kRolePermissionsRoleId = "role_id";
    constexpr std::string_view kRolePermissionsPermissionId = "permission_id";

    // Creates the role_permissions table
    void MakeRolePermissionsTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema