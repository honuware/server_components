#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "sql_util/database_common.h"          // Transaction, DatabaseHelper
#include "util/secrets/secrets_helper.h"
#include "util/secrets/secret_keys.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/cookie_manager.h"
#include "business_logic/auth/server_config.h"
#include "sql_util/table_helpers/sessions.h"

// Role/permission helpers
#include "sql_util/table_helpers/roles.h"
#include "sql_util/table_helpers/permissions.h"
#include "sql_util/table_helpers/role_permissions.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "db_schema/roles.h"
#include "db_schema/permissions.h"
#include "db_schema/role_permissions.h"
#include "db_schema/role_assignments.h"

namespace Auth {

class Session {
public:
    Session(DatabaseHelper databaseHelper);
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session() = default;

    bool IsLoggedIn() const;
    int64_t GetPersonId() const;

    // Phase 4.1 of the security review: the value of the `csrft`
    // cookie that this Session set on the response. Empty when the
    // session was loaded from an existing cookie (no rotation
    // happens on InitializeFromFromCookie). Tests use this to
    // assemble matching X-CSRF-Token requests; production code does
    // not need to read it because the SPA reads the cookie value
    // directly via document.cookie.
    const std::string& GetCsrfToken() const { return csrfToken_; }

    void InitializeFromLogin(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        std::string_view sessionToken,
        CookieManagerPtr cookieManager,
        std::string_view email,
        bool remember);

    bool InitializeFromFromCookie(
        Transaction& transaction,
        CookieManagerPtr cookieManager);

    bool InitializeFromDeviceToken(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        CookieManagerPtr cookieManager);

    // Role/permission checks (with transaction parameter)
    bool IsAdmin(Transaction& transaction);
    bool ActiveUserHasRole(Transaction& transaction, std::string_view roleName);
    bool ActiveUserHasPermission(Transaction& transaction, std::string_view permissionName);

private:
    DatabaseHelper databaseHelper_;
    int64_t personId_ = -1;
    std::string csrfToken_;

    void SetDeviceTokenCookie(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        std::string_view tokenId,
        std::string_view base64EncodedSecret,
        CookieManagerPtr cookieManager);
    void SetCookieHelper(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        std::string_view key,
        std::string_view value,
        int64_t maxAgeMicros,
        CookieManagerPtr cookieManager);

    // Phase 4.1 of the security review: generates a fresh 32-byte
    // random CSRF token, base64-encodes it, sets it as the `csrft`
    // cookie (NOT HttpOnly so the SPA can read it), and stores the
    // value in csrfToken_. Same Max-Age as the session cookie so
    // the two expire together. Called from InitializeFromLogin and
    // InitializeFromDeviceToken.
    void SetCsrfTokenCookie(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        int64_t maxAgeMicros,
        CookieManagerPtr cookieManager);
};

} // namespace Auth