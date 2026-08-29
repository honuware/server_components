#include "business_logic/create_framework_tables.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db_schema/admin_alerts.h"
#include "db_schema/admin_column_data_info.h"
#include "db_schema/admin_column_enums.h"
#include "db_schema/admin_column_friendly_names.h"
#include "db_schema/admin_column_redactions.h"
#include "db_schema/admin_enum_values.h"
#include "db_schema/admin_enums.h"
#include "db_schema/admin_nested_tables.h"
#include "db_schema/admin_table_display_template.h"
#include "db_schema/admin_table_friendly_names.h"
#include "db_schema/admin_table_permissions.h"
#include "db_schema/admin_top_level_tables.h"
#include "db_schema/allowed_tables.h"
#include "db_schema/auth_events.h"
#include "db_schema/config_secrets.h"
#include "db_schema/device_tokens.h"
#include "db_schema/email_verifications.h"
#include "db_schema/idempotency_keys.h"
#include "db_schema/login_attempts.h"
#include "db_schema/permission_implications.h"
#include "db_schema/permissions.h"
#include "db_schema/people.h"
#include "db_schema/photos.h"
#include "db_schema/site_assets.h"
#include "db_schema/site_fonts.h"
#include "db_schema/role_assignments.h"
#include "db_schema/role_permissions.h"
#include "db_schema/roles.h"
#include "db_schema/schema_migrations.h"
#include "db_schema/sessions.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/database_access/db_and_table_operations.h"
#include "sql_util/database_common.h"
#include "sql_util/table_helpers/admin_column_redactions.h"
#include "sql_util/table_helpers/permissions.h"
#include "sql_util/table_helpers/roles.h"
#include "util/secrets/secret_values.h"
#include "util/types.h"

namespace {

// DbPair (sql_util/database_common.h) and AddToKeyValueTable (util/types.h) are
// reused from honuware; only the row-inserter lambda is local.

// Returns a lambda that inserts one row into `tableName`. Captures databaseHelper
// and tableName BY VALUE (the returned lambda outlives this call) — a [&] capture
// dangles the string_view on Linux; `transaction` is a reference to the caller's
// long-lived, non-copyable Transaction. (Same rule as the app's create_database.)
auto MakeAddRowLambda(
    Transaction& transaction, DatabaseHelper databaseHelper,
    std::string_view tableName) {
    return [&transaction, databaseHelper, tableName](
               const KeyValueTable& keyValueTable) {
        DbCrud::AddRowToTable(
            transaction, databaseHelper, tableName, keyValueTable);
    };
}

}  // namespace

// Every framework table, in FK / creation order.
//
// Exposed as DATA rather than left as a sequence of calls so a test can compare
// it against MakeFrameworkTables. The two lists have to agree, and for
// site_assets they silently did not: it was registered but never created, so no
// database ever had the table and only the repair migration produced it. A list
// can be diffed; a hundred function calls cannot.
const std::vector<std::string_view>& FrameworkTableCreationOrder() {
    static const std::vector<std::string_view> tables = {
        DbSchema::kSchemaMigrationsTable,
        DbSchema::kAdminAlertsTable,
        DbSchema::kAdminTopLevelTablesTable,
        DbSchema::kAdminNestedTablesTable,
        DbSchema::kAdminColumnDataInfoTable,
        DbSchema::kAdminColumnRedactionsTable,
        DbSchema::kAdminEnumsTable,
        DbSchema::kAdminEnumValuesTable,
        DbSchema::kAdminColumnEnumsTable,
        DbSchema::kAdminColumnFriendlyNamesTable,
        DbSchema::kAdminTableFriendlyNamesTable,
        DbSchema::kAdminTableDisplayTemplateTable,
        DbSchema::kAllowedTablesTable,
        DbSchema::kConfigSecretsTable,
        DbSchema::kPeopleTable,
        DbSchema::kRolesTable,
        DbSchema::kPermissionsTable,
        DbSchema::kAdminTablePermissionsTable,
        DbSchema::kRoleAssignmentsTable,
        DbSchema::kRolePermissionsTable,
        DbSchema::kPermissionImplicationsTable,
        DbSchema::kSessionsTable,
        DbSchema::kDeviceTokensTable,
        DbSchema::kEmailVerificationsTable,
        DbSchema::kPhotoSupportTables,
        DbSchema::kPhotoInstances,
        DbSchema::kSourcePhotos,
        DbSchema::kScaledPhotos,
        DbSchema::kTableItemPhotos,
        DbSchema::kSiteFontSources,
        DbSchema::kSiteFonts,
        DbSchema::kSiteFontFaces,
        // MISSING from this list from the day it was added (Tenant Theming
        // Phase 9). It was registered in MakeFrameworkTables, so the schema
        // looked complete and the DDL existed — but nothing ever ran it, so NO
        // database has ever had this table from creation. Framework migration
        // 0001_site_assets was written to repair "older" databases and was in
        // fact the only thing that ever created it, which is why a site that
        // had never run --migrate could not store a theme's images.
        DbSchema::kSiteAssets,
        DbSchema::kIdempotencyKeysTable,
        DbSchema::kLoginAttemptsTable,
        DbSchema::kAuthEventsTable,
    };
    return tables;
}

void CreateFrameworkTables(
    Transaction& transaction, DbSchema::DatabaseInfo databaseInfo) {
    for (std::string_view tableName : FrameworkTableCreationOrder()) {
        DbOps::DropIfExistsAndCreateTable(transaction, databaseInfo, tableName);
    }
    // Indexes AFTER their tables — all three targets are created above, so the
    // interleaving the previous version used is not required.
    DbSchema::CreatePermissionImplicationsIndexes(transaction);
    DbSchema::CreateLoginAttemptsIndexes(transaction);
    DbSchema::CreateAuthEventsIndexes(transaction);
}

void PopulateFrameworkTables(
    Transaction& transaction, DatabaseHelper databaseHelper,
    DbSchema::DatabaseInfo databaseInfo) {
    (void)databaseInfo;

    // --- Framework config-secret defaults -----------------------------------
    {
        auto AddSecret = [&](std::string_view name, std::string_view value) {
            MakeAddRowLambda(
                transaction, databaseHelper, DbSchema::kConfigSecretsTable)(
                { DbPair(DbSchema::kConfigSecretsName, name),
                  DbPair(DbSchema::kConfigSecretsValue, value) });
        };
        Secrets::Values::FillInSecretsStringView(
            [&](std::string_view name, std::string_view value) {
                AddSecret(name, value);
            });
    }

    // --- Base roles ----------------------------------------------------------
    {
        auto AddRole = [&](std::string_view name, std::string_view description) {
            MakeAddRowLambda(transaction, databaseHelper, DbSchema::kRolesTable)(
                { DbPair(DbSchema::kRolesName, name),
                  DbPair(DbSchema::kRolesDescription, description) });
        };
        AddRole(DbSchema::kRoleNameAdmin,
            "Administrator role with full permissions.");
        AddRole(DbSchema::kRoleNameUser,
            "Standard user role with limited permissions.");
    }

    // --- Base framework permissions ------------------------------------------
    {
        auto AddPermission = [&](std::string_view name,
                                 std::string_view description) {
            MakeAddRowLambda(
                transaction, databaseHelper, DbSchema::kPermissionsTable)(
                { DbPair(DbSchema::kPermissionsName, name),
                  DbPair(DbSchema::kPermissionsDescription, description),
                  DbPair(DbSchema::kPermissionsIsPricingEligible, "false") });
        };
        AddPermission("admin_portal", "Permission to manage user accounts.");
        // Gates the framework /api/staff/* endpoints (quick account creation,
        // etc.). Granted to Administrator below.
        AddPermission(DbSchema::kPermissionStaffAccess,
            "Permission to operate staff endpoints (check-in, drop-in "
            "booking, quick account creation, …).");
    }

    // --- Administrator role -> framework permission grants -------------------
    // Referenced by NAME so the split is robust to id ordering (consumers append
    // their own roles/permissions afterward).
    {
        TableHelpers::Roles rolesHelper(databaseHelper);
        TableHelpers::Permissions permissionsHelper(databaseHelper);
        int64_t adminRoleId = std::stoll(
            rolesHelper.GetRole(transaction, DbSchema::kRoleNameAdmin)
                .at(std::string(DbSchema::kRolesId)));
        auto PermissionId = [&](std::string_view name) {
            return std::stoll(
                permissionsHelper.GetPermission(transaction, name)
                    .at(std::string(DbSchema::kPermissionsId)));
        };
        auto AddRolePermission = [&](int64_t roleId, int64_t permissionId) {
            MakeAddRowLambda(
                transaction, databaseHelper, DbSchema::kRolePermissionsTable)(
                { DbPair(DbSchema::kRolePermissionsRoleId,
                         std::to_string(roleId)),
                  DbPair(DbSchema::kRolePermissionsPermissionId,
                         std::to_string(permissionId)) });
        };
        AddRolePermission(adminRoleId, PermissionId("admin_portal"));
        AddRolePermission(
            adminRoleId, PermissionId(DbSchema::kPermissionStaffAccess));
    }

    // --- Allowed tables (framework) ------------------------------------------
    {
        auto AddAllowedTable = [&](std::string_view allowedTableName) {
            MakeAddRowLambda(
                transaction, databaseHelper, DbSchema::kAllowedTablesTable)(
                { DbPair(DbSchema::kAllowedTablesName, allowedTableName) });
        };
        AddAllowedTable(DbSchema::kPermissionImplicationsTable);
    }

    // --- Photo support (framework) -------------------------------------------
    {
        MakeAddRowLambda(
            transaction, databaseHelper, DbSchema::kPhotoSupportTables)(
            { DbPair(DbSchema::kPhotoSupportTablesTableName,
                     DbSchema::kPeopleTable) });
    }

    // --- Column redactions (security) ----------------------------------------
    {
        TableHelpers::AdminColumnRedactions helper(databaseHelper);
        auto AddRedaction = [&](std::string_view tableName,
                                std::string_view columnName) {
            helper.AddAdminColumnRedaction(transaction, tableName, columnName);
        };
        AddRedaction(DbSchema::kPeopleTable, DbSchema::kPeoplePasswordHash);
        AddRedaction(
            DbSchema::kDeviceTokensTable, DbSchema::kDeviceTokensSecretHash);
        AddRedaction(DbSchema::kEmailVerificationsTable,
            DbSchema::kEmailVerificationsTokenHash);
        AddRedaction(DbSchema::kSessionsTable, DbSchema::kSessionsUuid);
    }

    // --- Admin top-level tables (framework) ----------------------------------
    {
        auto AddRow = [&](std::string_view name) {
            MakeAddRowLambda(transaction, databaseHelper,
                DbSchema::kAdminTopLevelTablesTable)(
                { DbPair(DbSchema::kAdminTopLevelTablesName, name) });
        };
        AddRow(DbSchema::kAdminAlertsTable);
        AddRow(DbSchema::kPermissionImplicationsTable);
        AddRow(DbSchema::kPeopleTable);
        AddRow(DbSchema::kPermissionsTable);
        AddRow(DbSchema::kRolesTable);
        AddRow(DbSchema::kRoleAssignmentsTable);
        AddRow(DbSchema::kRolePermissionsTable);
    }

    // --- Admin nested tables (framework) -------------------------------------
    {
        auto AddRow = [&](std::string_view name) {
            MakeAddRowLambda(transaction, databaseHelper,
                DbSchema::kAdminNestedTablesTable)(
                { DbPair(DbSchema::kAdminNestedTablesName, name) });
        };
        AddRow(DbSchema::kRoleAssignmentsTable);
        AddRow(DbSchema::kRolePermissionsTable);
    }

    // --- Admin column data-info (framework) ----------------------------------
    {
        auto AddRow = [&](std::string_view tableName, std::string_view columnName,
                          std::string_view label, std::string_view hint,
                          std::string_view htmlInputType,
                          std::string_view required, std::string_view hidden = "",
                          std::string_view readonly_ = "") {
            KeyValueTable kvt = {
                DbPair(DbSchema::kAdminColumnDataInfoTableName, tableName),
                DbPair(DbSchema::kAdminColumnDataInfoColumnName, columnName),
                DbPair(DbSchema::kAdminColumnDataInfoLabel, label),
                DbPair(DbSchema::kAdminColumnDataInfoHint, hint),
                DbPair(DbSchema::kAdminColumnDataInfoHtmlInputType, htmlInputType),
                DbPair(DbSchema::kAdminColumnDataInfoRequired, required) };
            if (!hidden.empty())
                AddToKeyValueTable(kvt, DbSchema::kAdminColumnDataInfoHidden, hidden);
            if (!readonly_.empty())
                AddToKeyValueTable(
                    kvt, DbSchema::kAdminColumnDataInfoReadonly, readonly_);
            MakeAddRowLambda(
                transaction, databaseHelper, DbSchema::kAdminColumnDataInfoTable)(kvt);
        };
        AddRow(DbSchema::kPermissionsTable, DbSchema::kPermissionsIsPricingEligible, "Offered for Pricing", "Customer-facing tier shown in the product pricing matrix and class-access pickers", "bool", "true");
        AddRow(DbSchema::kPermissionImplicationsTable, DbSchema::kPermissionImplicationsPermissionId, "Permission", "Permission that confers the implied one (permissions.id)", "number", "true");
        AddRow(DbSchema::kPermissionImplicationsTable, DbSchema::kPermissionImplicationsImpliesPermissionId, "Implies Permission", "Permission additionally conferred (permissions.id)", "number", "true");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleEmail, "Email", "Email address of the person", "text", "true");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleFirstName, "First Name", "First name of the person", "text", "true");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleLastName, "Last Name", "Last name of the person", "text", "true");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeoplePasswordHash, "Password Hash", "Hashed password for authentication", "text", "true");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleMustChangePassword, "Must Change Password", "Requires password change on next login", "bool", "true");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleEmailVerifiedAt, "Email Verified", "When the person verified their email address", "date", "false", "", "true");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleCreatedAt, "Created", "When this person was created", "date", "false", "", "true");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleUpdatedAt, "Updated", "When this person was last updated", "date", "false", "", "true");
        AddRow(DbSchema::kPermissionsTable, DbSchema::kPermissionsName, "Name", "Permission name", "text", "true");
        AddRow(DbSchema::kPermissionsTable, DbSchema::kPermissionsDescription, "Description", "Permission description", "text", "true");
        AddRow(DbSchema::kRolesTable, DbSchema::kRolesName, "Name", "Role name", "text", "true");
        AddRow(DbSchema::kRolesTable, DbSchema::kRolesDescription, "Description", "Role description", "text", "true");
        AddRow(DbSchema::kRolePermissionsTable, DbSchema::kRolePermissionsRoleId, "Role", "Role to assign permission to", "number", "true", "", "true");
        AddRow(DbSchema::kRolePermissionsTable, DbSchema::kRolePermissionsPermissionId, "Permission", "Permission to assign", "number", "true");
        AddRow(DbSchema::kRoleAssignmentsTable, DbSchema::kRoleAssignmentsPersonId, "Person", "Person assigned to role", "number", "true", "", "true");
        AddRow(DbSchema::kRoleAssignmentsTable, DbSchema::kRoleAssignmentsRoleId, "Role", "Role assigned", "number", "true", "", "true");
    }

    // --- Admin column friendly names (framework) -----------------------------
    {
        auto AddRow = [&](std::string_view tableName, std::string_view columnName,
                          std::string_view friendlyName) {
            MakeAddRowLambda(transaction, databaseHelper,
                DbSchema::kAdminColumnFriendlyNamesTable)(
                { DbPair(DbSchema::kAdminColumnFriendlyNamesTableName, tableName),
                  DbPair(DbSchema::kAdminColumnFriendlyNamesColumnName, columnName),
                  DbPair(DbSchema::kAdminColumnFriendlyNamesFriendlyName, friendlyName) });
        };
        AddRow(DbSchema::kPermissionImplicationsTable, DbSchema::kPermissionImplicationsId, "ID");
        AddRow(DbSchema::kPermissionImplicationsTable, DbSchema::kPermissionImplicationsPermissionId, "Permission");
        AddRow(DbSchema::kPermissionImplicationsTable, DbSchema::kPermissionImplicationsImpliesPermissionId, "Implies Permission");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleId, "Person ID");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleEmail, "Email");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleFirstName, "First Name");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleLastName, "Last Name");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeoplePasswordHash, "Password Hash");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleMustChangePassword, "Must Change Password");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleEmailVerifiedAt, "Email Verified");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleCreatedAt, "Created");
        AddRow(DbSchema::kPeopleTable, DbSchema::kPeopleUpdatedAt, "Updated");
        AddRow(DbSchema::kPermissionsTable, DbSchema::kPermissionsId, "ID");
        AddRow(DbSchema::kPermissionsTable, DbSchema::kPermissionsName, "Permission Name");
        AddRow(DbSchema::kPermissionsTable, DbSchema::kPermissionsDescription, "Description");
        AddRow(DbSchema::kRolesTable, DbSchema::kRolesId, "ID");
        AddRow(DbSchema::kRolesTable, DbSchema::kRolesName, "Role Name");
        AddRow(DbSchema::kRolesTable, DbSchema::kRolesDescription, "Description");
        AddRow(DbSchema::kRolePermissionsTable, DbSchema::kRolePermissionsId, "ID");
        AddRow(DbSchema::kRolePermissionsTable, DbSchema::kRolePermissionsRoleId, "Role");
        AddRow(DbSchema::kRolePermissionsTable, DbSchema::kRolePermissionsPermissionId, "Permission");
        AddRow(DbSchema::kRoleAssignmentsTable, DbSchema::kRoleAssignmentsId, "ID");
        AddRow(DbSchema::kRoleAssignmentsTable, DbSchema::kRoleAssignmentsPersonId, "Person");
        AddRow(DbSchema::kRoleAssignmentsTable, DbSchema::kRoleAssignmentsRoleId, "Role");
    }

    // --- Admin table friendly names (framework) ------------------------------
    {
        auto AddRow = [&](std::string_view name, std::string_view friendlyName,
                          std::string_view description) {
            MakeAddRowLambda(transaction, databaseHelper,
                DbSchema::kAdminTableFriendlyNamesTable)(
                { DbPair(DbSchema::kAdminTableFriendlyNamesName, name),
                  DbPair(DbSchema::kAdminTableFriendlyNamesFriendlyName, friendlyName),
                  DbPair(DbSchema::kAdminTableFriendlyNamesDescription, description) });
        };
        AddRow(DbSchema::kPermissionImplicationsTable, "Permission Implications", "Directed edges of the permission hierarchy: holding a permission also confers the implied one.");
        AddRow(DbSchema::kPeopleTable, "People", "People with accounts on the site.");
        AddRow(DbSchema::kPermissionsTable, "Permissions", "Available permissions that can be assigned to roles.");
        AddRow(DbSchema::kRolesTable, "Roles", "User roles for access control.");
        AddRow(DbSchema::kRoleAssignmentsTable, "Role Assignments", "Roles assigned to people.");
        AddRow(DbSchema::kRolePermissionsTable, "Role Permissions", "Permissions assigned to roles.");
    }

    // --- Admin table display templates (framework) ---------------------------
    {
        auto AddRow = [&](std::string_view name, std::string_view displayTemplate) {
            MakeAddRowLambda(transaction, databaseHelper,
                DbSchema::kAdminTableDisplayTemplateTable)(
                { DbPair(DbSchema::kAdminTableDisplayTemplateName, name),
                  DbPair(DbSchema::kAdminTableDisplayTemplateTemplate, displayTemplate) });
        };
        AddRow(DbSchema::kPeopleTable, "{first_name} {last_name} - {email}");
        AddRow(DbSchema::kRolesTable, "{name}");
        AddRow(DbSchema::kPermissionImplicationsTable, "{permission_id} -> {implies_permission_id}");
        AddRow(DbSchema::kRolePermissionsTable, "{role_id} — {permission_id}");
        AddRow(DbSchema::kPermissionsTable, "{name}");
        AddRow(DbSchema::kRoleAssignmentsTable, "{person_id} — {role_id}");
    }
}
