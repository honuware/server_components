#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

    // Table and column identifiers
    constexpr std::string_view kRolesTable = "roles";
    constexpr std::string_view kRolesId = "id";
    constexpr std::string_view kRolesName = "name";
    constexpr std::string_view kRolesDescription = "description";

    // Role name constants
    constexpr std::string_view kRoleNameAdmin = "admin";
    constexpr std::string_view kRoleNameUser = "user";
    constexpr std::string_view kRoleNameTeachers = "teachers";
    constexpr std::string_view kRoleNameInstructor = "instructor";

    // Creates the roles table
    void MakeRolesTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema