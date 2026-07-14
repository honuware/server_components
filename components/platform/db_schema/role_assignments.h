#pragma once

#include "sql_util/schema/database_info.h"
#include "people.h"
#include "roles.h"

namespace DbSchema {

    // Table and column identifiers
    constexpr std::string_view kRoleAssignmentsTable = "role_assignments";
    constexpr std::string_view kRoleAssignmentsId = "id";
    constexpr std::string_view kRoleAssignmentsPersonId = "person_id";
    constexpr std::string_view kRoleAssignmentsRoleId = "role_id";

    // Creates the role_assignments table
    void MakeRoleAssignmentsTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema