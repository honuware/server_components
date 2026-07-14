#include "endpoint_test_helper.h"

#include "endpoints/web_app.h"
#include "db_schema/permissions.h"
#include "sql_util/table_helpers/admin_column_redactions.h"
#include "sql_util/table_helpers/allowed_tables.h"
#include "sql_util/table_helpers/admin_top_level_tables.h"
#include "sql_util/table_helpers/admin_nested_tables.h"
#include "sql_util/table_helpers/admin_table_permissions.h"
#include "sql_util/table_helpers/permissions.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "sql_util/table_helpers/role_permissions.h"
#include "sql_util/table_helpers/roles.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/mail/mail_helper_test_util.h"
#include "test/src/util/test_transaction_provider.h"
#include "business_logic/auth/cookie_manager_test_util.h"
#include "util/square/square_client_test_util.h"

namespace {

// Phase 3.8 of the security review: tests that want to exercise the
// JSON-boundary redaction populate the admin_column_redactions table
// BEFORE constructing EndpointTestHelper. The helper snapshots the
// table contents into the WebApp at construction time. Tests that
// don't insert redaction rows get an empty set — which preserves the
// pre-3.8 behavior of every existing endpoint test.
TableHelpers::ColumnRedactionSet LoadRedactionsAtTestStartup(
    Transaction& transaction, DatabaseHelper databaseHelper) {
    TableHelpers::AdminColumnRedactions helper(databaseHelper);
    return helper.LoadColumnRedactionSet(transaction);
}

}  // namespace

EndpointTestHelper::EndpointTestHelper(
    Transaction& transaction, TestDatabaseUtil& testDatabaseUtil)
    : testDatabaseUtil_(testDatabaseUtil),
      secretsHelper_(Secrets::Test::MakeTestSecretsHelper()),
      mailHelper_(Mail::Test::MakeTestMailHelper()),
      cookieManagerTest_(Auth::Test::MakeCookieManagerTest()),
      squareClient_(Square::Test::MakeTestSquareClient()),
      webApp_(
          testDatabaseUtil.GetDatabaseHelper(),
          testDatabaseUtil.GetDatabaseInfo().GetDatabaseName(),
          testDatabaseUtil.GetDatabaseInfo(),
          secretsHelper_,
          mailHelper_,
          MakeTestTransactionProvider(transaction),
          Auth::Test::MakeTestCookieManagerFactory(cookieManagerTest_),
          LoadRedactionsAtTestStartup(
              transaction, testDatabaseUtil.GetDatabaseHelper()))
{
    // Componentization Phase 1.6: register the (test) Square client in WebApp's
    // generic service registry under the BASE Square::SquareClient type, so
    // AppEndpointAuthHelper::GetSquareClient() (which looks it up by base type)
    // resolves to this test double. Mirrors main.cpp's production registration.
    webApp_.SetService<Square::SquareClient>(squareClient_);

    // NOTE: this reusable helper only collects already-registered routes. The
    // app-side test main (test/src/main.cpp) is responsible for anchoring the app
    // endpoint registration TU into the link (Endpoints::RegisterAllEndpoints),
    // so this helper carries no dependency on any particular app's endpoint set.
    RoutingBase::AddRoutes(webApp_);

    // Phase 4.2 of the security review: turn CsrfGuard off by default
    // for endpoint tests. Existing endpoint tests post to
    // /api/<route> with synthesized session cookies but no matching
    // X-CSRF-Token; making them all also build a CSRF token would be
    // a 80+ file churn for no test-quality gain. The dedicated
    // csrf_guard_test.cpp re-enables the guard explicitly to cover
    // the enforcement behavior.
    webApp_.GetApp().get_middleware<Endpoints::CsrfGuard>()
        .SetEnabled(false);

    // Phase 7.1 of the security review: turn SecurityHeaders off by
    // default for endpoint tests so existing tests that assert on
    // exact response shape (raw bodies, header counts) don't break
    // on the X-Content-Type-Options / Referrer-Policy / etc. that
    // would now ride along on every response. The dedicated
    // security_headers_test.cpp re-enables it.
    webApp_.GetApp().get_middleware<Endpoints::SecurityHeaders>()
        .SetEnabled(false);
}

EndpointTestHelper::~EndpointTestHelper() {
}

WebApp& EndpointTestHelper::GetWebApp() {
    return webApp_;
}

void EndpointTestHelper::AddAllowedTable(Transaction& transaction, std::string_view allowedTable) {
    TableHelpers::AllowedTables allowedTables(testDatabaseUtil_.GetDatabaseHelper());
    allowedTables.AddAllowedTable(transaction, allowedTable);
}

void EndpointTestHelper::AddAdminTopLevelTable(Transaction& transaction, std::string_view tableName) {
    TableHelpers::AdminTopLevelTables adminTopLevelTables(testDatabaseUtil_.GetDatabaseHelper());
    adminTopLevelTables.AddAdminTopLevelTable(transaction, tableName);
}

void EndpointTestHelper::AddAdminNestedTable(Transaction& transaction, std::string_view tableName) {
    TableHelpers::AdminNestedTables adminNestedTables(testDatabaseUtil_.GetDatabaseHelper());
    adminNestedTables.AddAdminNestedTable(transaction, tableName);
}

void EndpointTestHelper::AddAdminTablePermission(
    Transaction& transaction,
    std::string_view tableName,
    int64_t permissionId) {
    TableHelpers::AdminTablePermissions atp(testDatabaseUtil_.GetDatabaseHelper());
    atp.AddAdminTablePermission(transaction, tableName, permissionId);
}

void EndpointTestHelper::GrantPermissionToPerson(
    Transaction& transaction,
    int64_t personId,
    std::string_view permissionName) {
    // Phase 3.6 of the security review. Path: create the named
    // permission (or look it up if the test already added it),
    // create a per-permission role, link role -> permission, and
    // assign the role to the person. Tests run in their own
    // transaction so this state is rolled back at the end.
    auto db = testDatabaseUtil_.GetDatabaseHelper();
    TableHelpers::Permissions permissions(db);
    int64_t permissionId = 0;
    try {
        KeyValueTable existing = permissions.GetPermission(
            transaction, permissionName);
        permissionId = std::stoll(
            existing.at(std::string(DbSchema::kPermissionsId)));
    } catch (const std::exception&) {
        permissionId = permissions.AddPermission(
            transaction, permissionName,
            "Test-granted permission");
    }

    TableHelpers::Roles roles(db);
    std::string roleName = "test_" + std::string(permissionName);
    int64_t roleId = roles.AddRole(
        transaction, roleName, "Test role for " + std::string(permissionName));

    TableHelpers::RolePermissions rolePermissions(db);
    rolePermissions.AddRolePermission(transaction, roleId, permissionId);

    TableHelpers::RoleAssignments roleAssignments(db);
    roleAssignments.AddRoleAssignment(transaction, personId, roleId);
}

Secrets::Test::SecretsHelperTestPtr EndpointTestHelper::GetSecretsHelper() const {
    return secretsHelper_;
}

Mail::Test::TestMailHelperPtr EndpointTestHelper::GetMailHelper() const {
    return mailHelper_;
}

Auth::Test::CookieManagerTestPtr EndpointTestHelper::GetCookieManagerTest() const {
    return cookieManagerTest_;
}

Square::Test::TestSquareClientPtr EndpointTestHelper::GetSquareClient() const {
    return squareClient_;
}
