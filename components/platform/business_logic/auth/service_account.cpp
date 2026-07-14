#include "service_account.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

#include "business_logic/auth/person.h"
#include "db_schema/people.h"
#include "db_schema/permissions.h"
#include "db_schema/roles.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "sql_util/table_helpers/role_permissions.h"
#include "sql_util/table_helpers/roles.h"

namespace Auth {

bool IsServiceAccountEmail(std::string_view email, std::string_view domain) {
    if (email.size() < domain.size()) return false;
    return email.compare(
        email.size() - domain.size(), domain.size(), domain) == 0;
}

std::string ReadSchedulerServiceAccountPassword() {
    const char* value = std::getenv(
        std::string(kSchedulerServiceAccountPasswordEnvVar).c_str());
    if (value == nullptr || std::string(value).empty()) {
        throw std::runtime_error(
            "SCHEDULER_SERVICE_ACCOUNT_PASSWORD environment variable is not "
            "set. Generate a strong password and add it to the server "
            "environment file (or your equivalent --env-file) before running "
            "the database helper or the scheduler helper.");
    }
    return std::string(value);
}

void EnsureSchedulerServiceAccount(
    Transaction& transaction,
    DatabaseHelper databaseHelper,
    std::string_view schedulerEmail,
    std::string_view password) {

    // Idempotent guard: check by row presence rather than verified state.
    // PersonHelper::IsPerson() only returns true for verified rows, which
    // would let a partially-created row trigger a duplicate-insert on retry.
    TableHelpers::People people(databaseHelper);
    KeyValueTable existing = people.LookupPersonByEmail(
        transaction, schedulerEmail);
    if (!existing.empty()) {
        // Operators rotate by deleting the row and re-running the helper,
        // which is preferable to silently overwriting — they get a chance
        // to clear sessions and role assignments first.
        return;
    }

    // 1. Create the person row.
    PersonHelper personHelper(databaseHelper);
    PersonInfo info;
    info.email = std::string(schedulerEmail);
    info.firstName = std::string(kSchedulerServiceAccountFirstName);
    info.lastName = std::string(kSchedulerServiceAccountLastName);
    personHelper.CreateFullyValidatedUser(transaction, info, password);

    // Re-look up to get the generated id.
    KeyValueTable personRow = people.LookupPersonByEmail(
        transaction, schedulerEmail);
    if (personRow.empty()) {
        throw std::runtime_error(
            "EnsureSchedulerServiceAccount: failed to read back the row we "
            "just created");
    }
    int64_t personId = std::stoll(
        personRow.at(std::string(DbSchema::kPeopleId)));

    // 2. Look up or create the scheduler role. Use DbCrud directly because
    //    Roles::GetRole(name) throws on miss instead of returning an empty
    //    row, which is the wrong shape for "create if absent" logic.
    KeyValueTable roleRow = DbCrud::LookupRowByValue(
        transaction, databaseHelper,
        DbSchema::kRolesTable, DbSchema::kRolesName,
        kSchedulerRoleName);
    int64_t roleId;
    if (roleRow.empty()) {
        TableHelpers::Roles roles(databaseHelper);
        roleId = roles.AddRole(
            transaction, kSchedulerRoleName, kSchedulerRoleDescription);
    } else {
        roleId = std::stoll(roleRow.at(std::string(DbSchema::kRolesId)));
    }

    // 3. Link the role to the existing manage_subscriptions permission.
    //    PopulatePermissions in create_database.cpp creates this row before
    //    EnsureSchedulerServiceAccount runs. Same DbCrud-direct pattern as
    //    above so we get an empty-on-miss instead of an exception.
    KeyValueTable permRow = DbCrud::LookupRowByValue(
        transaction, databaseHelper,
        DbSchema::kPermissionsTable, DbSchema::kPermissionsName,
        kManageSubscriptionsPermission);
    if (permRow.empty()) {
        throw std::runtime_error(
            "EnsureSchedulerServiceAccount: manage_subscriptions permission "
            "is missing from the database. PopulatePermissions must run "
            "before this function.");
    }
    int64_t permId = std::stoll(
        permRow.at(std::string(DbSchema::kPermissionsId)));

    TableHelpers::RolePermissions rolePermissions(databaseHelper);
    rolePermissions.AddRolePermission(transaction, roleId, permId);

    // 3b. The scheduler also drives the class crons — send_signup_open_reminders
    //     (Classes Phase 11 §6), finalize_class_attendance,
    //     send_instructor_exception_digests, run_series_min_attendees_check,
    //     send_favorite_instructor_schedule_alerts (Classes Phase 13 §4.8) —
    //     all of which gate on manage_class_schedule. Grant it so those POSTs
    //     authorize. PopulatePermissions seeds this permission before this
    //     function runs in production; we grant it only when present so the
    //     auth unit tests that seed only manage_subscriptions are unaffected.
    KeyValueTable classSchedulePermRow = DbCrud::LookupRowByValue(
        transaction, databaseHelper,
        DbSchema::kPermissionsTable, DbSchema::kPermissionsName,
        DbSchema::kPermissionManageClassSchedule);
    if (!classSchedulePermRow.empty()) {
        int64_t classSchedulePermId = std::stoll(
            classSchedulePermRow.at(std::string(DbSchema::kPermissionsId)));
        rolePermissions.AddRolePermission(
            transaction, roleId, classSchedulePermId);
    }

    // 4. Assign the role to the service-account person.
    TableHelpers::RoleAssignments roleAssignments(databaseHelper);
    roleAssignments.AddRoleAssignment(transaction, personId, roleId);
}

}  // namespace Auth
