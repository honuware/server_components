#include "endpoint_auth_helper.h"

#include <set>
#include <string>

#include "business_logic/auth/cookie_manager.h"
#include "business_logic/tenancy/tenant_header.h"
#include "business_logic/tenancy/tenant_resolver.h"
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
#include "db_schema/tenants.h"
#include "util/logging.h"

EndpointAuthHelper::EndpointAuthHelper(
    WebApp& app,
    const crow::request& req,
    crow::response& res)
    : app_(app),
      request_(req),
      response_(res) {
    // Bind the session to the global database for now; ResolveTenant() rebinds it
    // to the tenant database once Initialize() resolves the request's tenant.
    session_.emplace(app.GetDatabaseHelper());
}

void EndpointAuthHelper::ResolveTenant() {
    // Route this request to its tenant, if tenancy is wired. TenantResolutionGuard
    // has already rejected any unknown/suspended/contradicting site upstream, so a
    // resolver hit here is a success; a miss means tenancy is inert (no resolver)
    // and we stay on the deployment's global provider.
    std::shared_ptr<Tenancy::TenantResolver> resolver = app_.GetTenantResolver();
    std::shared_ptr<Tenancy::TenantResourceRegistry> registry =
        app_.GetTenantResourceRegistry();
    if (resolver && registry) {
        std::string siteKey(
            request_.get_header_value(std::string(Tenancy::kSiteHeaderName)));
        std::optional<Tenancy::TenantContext> resolved = resolver->Resolve(siteKey);
        if (resolved.has_value() &&
            resolved->status != std::string(DbSchema::kTenantStatusSuspended)) {
            tenantContext_ = *resolved;
            tenantResources_ = registry->GetOrCreate(tenantContext_);
        }
    }

    // (Re)bind the session to the effective database — the tenant's when resolved,
    // the global otherwise — so session role/permission reads run against it.
    session_.emplace(GetDatabaseHelper());
}

void EndpointAuthHelper::Initialize() {
    ResolveTenant();
    cookieManager_ = app_.GetCookieManagerFactory()->CreateCookieManager(
        app_, request_, response_);
    try {
        GetTransactionProvider()->RunInTransaction(
            [&](Transaction& transaction) {
                session_->InitializeFromFromCookie(transaction, cookieManager_);
            });
    }
    catch (...) {
        // Swallow all errors here
    }
}

const Tenancy::TenantContext& EndpointAuthHelper::GetTenantContext() const {
    return tenantContext_;
}

Tenancy::TenantResourcesPtr EndpointAuthHelper::GetTenantResources() const {
    return tenantResources_;
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
    return *session_;
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
    // Re-pointed to the resolved tenant's provider (Phase 3.2); falls back to the
    // deployment's global provider when no tenant is resolved.
    if (tenantResources_ && tenantResources_->transactionProvider) {
        return tenantResources_->transactionProvider;
    }
    return app_.GetTransactionProvider();
}

DatabaseHelper EndpointAuthHelper::GetDatabaseHelper() const {
    // Re-pointed to the resolved tenant's database (Phase 3.2); falls back to the
    // deployment's global database when no tenant is resolved.
    if (tenantResources_ && tenantResources_->databaseHelper) {
        return *tenantResources_->databaseHelper;
    }
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
        if (session_->IsLoggedIn()) {
            std::set<std::string> tableSet(tables.begin(), tables.end());

            auto addTablesIfNew = [&](const StringArray& newTables) {
                for (const auto& t : newTables) {
                    if (tableSet.find(t) == tableSet.end()) {
                        tables.push_back(t);
                        tableSet.insert(t);
                    }
                }
            };

            if (session_->IsAdmin(transaction)) {
                // Admins get all admin tables
                TableHelpers::AdminTopLevelTables adminTopLevel(GetDatabaseHelper());
                addTablesIfNew(adminTopLevel.GetAdminTopLevelTables(transaction));

                TableHelpers::AdminNestedTables adminNested(GetDatabaseHelper());
                addTablesIfNew(adminNested.GetAdminNestedTables(transaction));
            } else {
                // Non-admin users: check admin_table_permissions for
                // tables granted through the user's permissions
                int64_t personId = session_->GetPersonId();
                TableHelpers::RoleAssignments ra(GetDatabaseHelper());
                KeyValueTableArray assignments = ra.GetRoleAssignmentsForPerson(
                    transaction, personId);

                // Collect all permission IDs the user has
                TableHelpers::RolePermissions rp(GetDatabaseHelper());
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
                    TableHelpers::AdminTablePermissions atp(GetDatabaseHelper());
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
    if (!session_->IsLoggedIn()) {
        resp = ErrorResponse::NotAuthenticated("Login required");
        return false;
    }
    try {
        if (!session_->ActiveUserHasPermission(transaction, permissionName)) {
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
