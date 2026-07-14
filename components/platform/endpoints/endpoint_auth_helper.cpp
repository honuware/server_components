#include "endpoint_auth_helper.h"

#include <set>

#include "business_logic/auth/cookie_manager.h"
#include "util/error_response.h"
#include "sql_util/table_helpers/admin_table_permissions.h"
#include "sql_util/table_helpers/admin_top_level_tables.h"
#include "sql_util/table_helpers/admin_nested_tables.h"
#include "sql_util/table_helpers/permissions.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "sql_util/table_helpers/role_permissions.h"
#include "db_schema/permissions.h"
#include "db_schema/role_assignments.h"
#include "db_schema/role_permissions.h"
#include "util/logging.h"

EndpointAuthHelper::EndpointAuthHelper(
    WebApp& app,
    const crow::request& req,
    crow::response& res)
    : app_(app),
      request_(req),
      response_(res),
      session_(app.GetDatabaseHelper()) {}

void EndpointAuthHelper::Initialize() {
    cookieManager_ = app_.GetCookieManagerFactory()->CreateCookieManager(
        app_, request_, response_);
    try {
        app_.GetTransactionProvider()->RunInTransaction(
            [&](Transaction& transaction) {
                session_.InitializeFromFromCookie(transaction, cookieManager_);
            });
    }
    catch (...) {
        // Swallow all errors here
    }
}

WebApp& EndpointAuthHelper::App() {
    return app_;
}

const crow::request& EndpointAuthHelper::Request() const {
    return request_;
}

crow::response& EndpointAuthHelper::Response() {
    return response_;
}

Auth::Session& EndpointAuthHelper::GetSession() {
    return session_;
}

DbSchema::DatabaseInfo EndpointAuthHelper::GetDatabaseInfo() const {
    return app_.GetDatabaseInfo();
}

Secrets::SecretsHelperPtr EndpointAuthHelper::GetSecretsHelper() const {
    return app_.GetSecretsHelper();
}

Mail::MailHelperPtr EndpointAuthHelper::GetMailHelper() const {
    return app_.GetMailHelper();
}

TransactionProviderPtr EndpointAuthHelper::GetTransactionProvider() const {
    return app_.GetTransactionProvider();
}

DatabaseHelper EndpointAuthHelper::GetDatabaseHelper() const {
    return app_.GetDatabaseHelper();
}

Auth::CookieManagerPtr EndpointAuthHelper::GetCookieManager() const {
    return cookieManager_;
}

const std::string& EndpointAuthHelper::GetDatabaseName() const {
    return app_.GetDatabaseName();
}

StringArray EndpointAuthHelper::GetAllowedTables(Transaction& transaction) {
    StringArray tables = app_.GetAllowedTables(transaction);

    try {
        if (session_.IsLoggedIn()) {
            std::set<std::string> tableSet(tables.begin(), tables.end());

            auto addTablesIfNew = [&](const StringArray& newTables) {
                for (const auto& t : newTables) {
                    if (tableSet.find(t) == tableSet.end()) {
                        tables.push_back(t);
                        tableSet.insert(t);
                    }
                }
            };

            if (session_.IsAdmin(transaction)) {
                // Admins get all admin tables
                TableHelpers::AdminTopLevelTables adminTopLevel(app_.GetDatabaseHelper());
                addTablesIfNew(adminTopLevel.GetAdminTopLevelTables(transaction));

                TableHelpers::AdminNestedTables adminNested(app_.GetDatabaseHelper());
                addTablesIfNew(adminNested.GetAdminNestedTables(transaction));
            } else {
                // Non-admin users: check admin_table_permissions for
                // tables granted through the user's permissions
                int64_t personId = session_.GetPersonId();
                TableHelpers::RoleAssignments ra(app_.GetDatabaseHelper());
                KeyValueTableArray assignments = ra.GetRoleAssignmentsForPerson(
                    transaction, personId);

                // Collect all permission IDs the user has
                TableHelpers::RolePermissions rp(app_.GetDatabaseHelper());
                std::set<int64_t> userPermissionIds;
                for (const auto& kv : assignments) {
                    int64_t roleId = std::stoll(
                        kv.at(std::string(DbSchema::kRoleAssignmentsRoleId)));
                    KeyValueTableArray rpRows = rp.GetRolePermissionsForRole(
                        transaction, roleId);
                    for (const auto& rpKv : rpRows) {
                        int64_t permId = std::stoll(
                            rpKv.at(std::string(DbSchema::kRolePermissionsPermissionId)));
                        userPermissionIds.insert(permId);
                    }
                }

                // For each permission, find tables that require it
                if (!userPermissionIds.empty()) {
                    TableHelpers::AdminTablePermissions atp(app_.GetDatabaseHelper());
                    for (int64_t permId : userPermissionIds) {
                        StringArray permTables = atp.GetTablesForPermissionId(
                            transaction, permId);
                        addTablesIfNew(permTables);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        LogWarning() << "GetAllowedTables admin check failed: " << e.what() << std::endl;
    } catch (...) {
        LogWarning() << "GetAllowedTables admin check failed with unknown exception" << std::endl;
    }

    return tables;
}

bool EndpointAuthHelper::IsTableAllowed(
    Transaction& transaction, std::string_view tableName) {
    StringArray tables = GetAllowedTables(transaction);
    std::set<std::string> tableSet(tables.begin(), tables.end());
    return tableSet.find(std::string(tableName)) != tableSet.end();
}

bool EndpointAuthHelper::RequirePermission(
    Transaction& transaction,
    std::string_view permissionName,
    crow::response& resp) {
    if (!session_.IsLoggedIn()) {
        resp = ErrorResponse::NotAuthenticated("Login required");
        return false;
    }
    try {
        if (!session_.ActiveUserHasPermission(transaction, permissionName)) {
            resp = ErrorResponse::NotAuthorized(
                "Missing required permission");
            return false;
        }
    } catch (const std::exception&) {
        // ActiveUserHasPermission throws when the named permission
        // hasn't been seeded yet (or any other DB issue). Fail closed
        // — don't leak the underlying message.
        resp = ErrorResponse::NotAuthorized(
            "Missing required permission");
        return false;
    }
    return true;
}

const TableHelpers::ColumnRedactionSet&
EndpointAuthHelper::GetColumnRedactions() const {
    return app_.GetColumnRedactions();
}
