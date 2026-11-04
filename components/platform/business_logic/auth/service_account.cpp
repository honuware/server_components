#include "service_account.h"

#include <cstdlib>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

#include "business_logic/auth/person.h"
#include "db_schema/people.h"
#include "db_schema/permissions.h"
#include "db_schema/role_permissions.h"
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

namespace {

// The permission's id, or nullopt when no `permissions` row has that name.
// DbCrud directly: an empty row on a miss, not an exception.
std::optional<int64_t> LookupPermissionId(
    Transaction& transaction,
    DatabaseHelper databaseHelper,
    std::string_view permissionName) {
    KeyValueTable permRow = DbCrud::LookupRowByValue(
        transaction, databaseHelper,
        DbSchema::kPermissionsTable, DbSchema::kPermissionsName,
        permissionName);
    if (permRow.empty()) return std::nullopt;
    return std::stoll(permRow.at(std::string(DbSchema::kPermissionsId)));
}

}  // namespace

void EnsureSchedulerServiceAccount(
    Transaction& transaction,
    DatabaseHelper databaseHelper,
    std::string_view schedulerEmail,
    std::string_view password,
    const std::vector<std::string_view>& permissionNames) {

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

    // 0. Resolve every permission before writing anything, so a missing one
    //    leaves no half-made account behind. run_scheduled_jobs comes from
    //    PopulateFrameworkTables; the app's names from its own seed. Both
    //    must run first.
    std::vector<std::string_view> grants = {
        DbSchema::kPermissionRunScheduledJobs};
    grants.insert(grants.end(), permissionNames.begin(), permissionNames.end());
    std::vector<int64_t> permissionIds;
    for (std::string_view name : grants) {
        std::optional<int64_t> permissionId =
            LookupPermissionId(transaction, databaseHelper, name);
        if (!permissionId) {
            throw std::runtime_error(
                "EnsureSchedulerServiceAccount: the " + std::string(name) +
                " permission is missing from the database. The permission "
                "seed must run before this function.");
        }
        permissionIds.push_back(*permissionId);
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

    // 3. Link the role to each permission it doesn't already hold. The role
    //    survives a password rotation (delete the person, re-run), so its
    //    links may already be there; a repeated name links once.
    TableHelpers::RolePermissions rolePermissions(databaseHelper);
    std::set<int64_t> linkedPermissionIds;
    for (const KeyValueTable& link :
         rolePermissions.GetRolePermissionsForRole(transaction, roleId)) {
        linkedPermissionIds.insert(std::stoll(
            link.at(std::string(DbSchema::kRolePermissionsPermissionId))));
    }
    for (int64_t permissionId : permissionIds) {
        if (linkedPermissionIds.insert(permissionId).second) {
            rolePermissions.AddRolePermission(
                transaction, roleId, permissionId);
        }
    }

    // 4. Assign the role to the service-account person.
    TableHelpers::RoleAssignments roleAssignments(databaseHelper);
    roleAssignments.AddRoleAssignment(transaction, personId, roleId);
}

}  // namespace Auth
