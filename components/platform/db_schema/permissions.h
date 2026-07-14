#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

    // Table and column identifiers
    constexpr std::string_view kPermissionsTable = "permissions";
    constexpr std::string_view kPermissionsId = "id";
    constexpr std::string_view kPermissionsName = "name";
    constexpr std::string_view kPermissionsDescription = "description";
    // Classes Phase 7 §10.2 — marks a permission as a customer-facing tier that
    // should be offered in the product pricing matrix and class-access requirement
    // pickers. Internal permissions (staff_access, manage_*, etc.) stay false so
    // they don't clutter those editors. Default false; toggled on the dedicated
    // Membership Tiers /manage page.
    constexpr std::string_view kPermissionsIsPricingEligible = "is_pricing_eligible";

    // Canonical permission names. Phase 3.6 of the security review:
    // staff endpoints (/api/staff/*) gate on this permission rather
    // than on raw IsLoggedIn — admins inherit it via role_permissions
    // seeding in create_database.cpp.
    constexpr std::string_view kPermissionStaffAccess = "staff_access";

    // Classes Phase 1 §8 — gates all /api/admin/class_schedule[s]/*
    // endpoints. Admins inherit it via role_permissions seeding; future
    // "Studio Manager" role (Phase 1 §8 follow-up) will also include it.
    constexpr std::string_view kPermissionManageClassSchedule = "manage_class_schedule";

    // Classes Phase 3 §5.4 — gates the staff skill-management endpoints
    // (/api/staff/person/<id>/skill[s]). Granted to admin + Studio Manager via
    // role_permissions seeding in create_database.cpp.
    constexpr std::string_view kPermissionManageSkills = "manage_skills";

    // Classes Phase 10 §5.3 — gates the admin "who's teaching what" grid
    // (/api/admin/instructor_load). Granted to admin + Studio Manager via
    // role_permissions seeding in create_database.cpp.
    constexpr std::string_view kPermissionViewAdminInstructorLoad =
        "view_admin_instructor_load";

    // Creates the permissions table
    void MakePermissionsTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema