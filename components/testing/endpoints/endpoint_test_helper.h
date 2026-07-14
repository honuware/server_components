#pragma once

#include <memory>

#include "endpoints/web_app.h"
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
    EndpointTestHelper(
        Transaction& transaction, TestDatabaseUtil& testDatabaseUtil);
    EndpointTestHelper(const EndpointTestHelper&) = delete;
    EndpointTestHelper& operator=(const EndpointTestHelper&) = delete;
    ~EndpointTestHelper();

    WebApp& GetWebApp();
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
    WebApp webApp_;
};