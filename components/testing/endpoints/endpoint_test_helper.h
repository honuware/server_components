#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "endpoints/web_app.h"
#include "business_logic/tenancy/tenant_context.h"
#include "test/src/util/database_test_helper.h"

namespace Secrets {
namespace Test {

class SecretsHelperTest;
using SecretsHelperTestPtr = std::shared_ptr<SecretsHelperTest>;

}  // namespace Test {
}  // namespace Secrets {

namespace Mail {
namespace Test {

    class TestMailHelper;
    using TestMailHelperPtr = std::shared_ptr<TestMailHelper>;

}  // namespace Test {
}  // namespace Mail

namespace Auth::Test {

    class CookieManagerTest;
    using CookieManagerTestPtr = std::shared_ptr<CookieManagerTest>;

}

namespace Square::Test {

    class TestSquareClient;
    using TestSquareClientPtr = std::shared_ptr<TestSquareClient>;

}

class EndpointTestHelper {
public:
    // The site key of the default fixed tenant installed at construction
    // (tenancy plan Phase 3.4). Existing endpoint tests send no site header, so
    // the FixedTenantResolver resolves them via its empty-key path and every test
    // routes to the test database with zero per-test edits. A test that wants to
    // exercise a matching header can send this value.
    static constexpr std::string_view kDefaultTestSiteKey = "test-site";

    EndpointTestHelper(
        Transaction& transaction, TestDatabaseUtil& testDatabaseUtil);
    EndpointTestHelper(const EndpointTestHelper&) = delete;
    EndpointTestHelper& operator=(const EndpointTestHelper&) = delete;
    ~EndpointTestHelper();

    WebApp& GetWebApp();

    // The two contexts seeded + resolved by InstallControlModeTenants.
    struct ControlTenants {
        Tenancy::TenantContext a;
        Tenancy::TenantContext b;
    };

    // Switches the WebApp to control mode (tenancy plan Phase 3.4): seeds two rows
    // in the test database's own `tenants` table (both pointing at the test DB —
    // the cheap same-DB routing default) and installs a ControlDbTenantResolver
    // over the test transaction provider. Returns the two resolved contexts so a
    // test can assert distinct site keys route to distinct TenantResources, and
    // drive the guard's missing/unknown/suspended edge cases. Call inside the
    // test transaction, after construction.
    ControlTenants InstallControlModeTenants(
        Transaction& transaction,
        std::string_view siteKeyA,
        std::string_view siteKeyB);
    void AddAllowedTable(
        Transaction& transaction, std::string_view allowedTable);
    void AddAdminTopLevelTable(
        Transaction& transaction, std::string_view tableName);
    void AddAdminNestedTable(
        Transaction& transaction, std::string_view tableName);
    void AddAdminTablePermission(
        Transaction& transaction,
        std::string_view tableName,
        int64_t permissionId);

    // Phase 3.6 of the security review: grant a named permission to a
    // person via a "test_<permission>" role. Creates the permission and
    // role on demand. Tests for permission-gated endpoints (e.g.
    // /api/staff/*) call this once during setup so the test user
    // satisfies EndpointAuthHelper::RequirePermission.
    void GrantPermissionToPerson(
        Transaction& transaction,
        int64_t personId,
        std::string_view permissionName);

    Secrets::Test::SecretsHelperTestPtr GetSecretsHelper() const;
    Mail::Test::TestMailHelperPtr GetMailHelper() const;
    Auth::Test::CookieManagerTestPtr GetCookieManagerTest() const;
    Square::Test::TestSquareClientPtr GetSquareClient() const;

private:
    TestDatabaseUtil& testDatabaseUtil_;
    Secrets::Test::SecretsHelperTestPtr secretsHelper_;
    Mail::Test::TestMailHelperPtr mailHelper_;
    Auth::Test::CookieManagerTestPtr cookieManagerTest_;
    Square::Test::TestSquareClientPtr squareClient_;
    // The one test transaction provider (wraps the test's aborted transaction).
    // Declared before webApp_ so it can be passed to the WebApp constructor AND
    // reused by the tenant-resource factory, keeping every tenant's DB work on the
    // SAME test transaction (so the test's rollback still cleans everything up).
    TransactionProviderPtr testTransactionProvider_;
    WebApp webApp_;
};