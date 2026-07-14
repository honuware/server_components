#include "session.h"

#include <stdexcept>

#include "auth_cookies.h"
#include "auth_helper.h"
#include "db_schema/device_tokens.h"
#include "sql_util/table_helpers/device_tokens.h"

namespace Auth {

Session::Session(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

bool Session::IsLoggedIn() const {
    return personId_ != -1;
}

int64_t Session::GetPersonId() const {
    return personId_;
}

void Session::InitializeFromLogin(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string_view sessionToken,
    CookieManagerPtr cookieManager,
    std::string_view email,
    bool remember) {
    if (!secrets) {
        throw std::runtime_error("Session::InitializeFromLogin - secrets null");
    }
    if (!cookieManager) {
        throw std::runtime_error("Session::InitializeFromLogin - cookieManager null");
    }

    Auth::PersonHelper personHelper(databaseHelper_);

    int64_t outPersonId = -1;
    if (!personHelper.LookupPersonIdBySessionToken(transaction, sessionToken, outPersonId)) {
        throw std::runtime_error("Session::InitializeFromLogin - invalid session token");
    }

    personId_ = outPersonId;

    // Max-Age
    int64_t maxAgeInMicros = -1;
    std::string maxAgeStr = secrets->LookupSecret(
        transaction, Secrets::kAuthSessionMaxDuractioninMicros);
    if (!maxAgeStr.empty()) {
        try {
            maxAgeInMicros = std::stoll(maxAgeStr);
        } catch (...) {
            maxAgeInMicros = -1;
        }
    }

    SetCookieHelper(transaction, secrets, "session_token", sessionToken,
        maxAgeInMicros, cookieManager);

    // Phase 4.1 of the security review: companion CSRF cookie. Set
    // in the same response as the session cookie so the SPA always
    // has a fresh, matching pair to work with.
    SetCsrfTokenCookie(transaction, secrets, maxAgeInMicros, cookieManager);

    if (remember) {
        // Also set device token cookie
        std::string tokenId;
        std::string base64EncodedSecret;
        if (personHelper.CreateDeviceToken(
            transaction, secrets, email, tokenId, base64EncodedSecret)) {
            SetDeviceTokenCookie(
                transaction, secrets, tokenId, base64EncodedSecret, cookieManager);
        }
    }
}

bool Session::InitializeFromFromCookie(
    Transaction& transaction,
    CookieManagerPtr cookieManager) {
    if (!cookieManager) {
        return false;
    }
    auto cookies = cookieManager->GetCookies();
    auto it = cookies.find("session_token");
    if (it == cookies.end()) {
        return false;
    }
    const std::string& token = it->second;

    Auth::PersonHelper personHelper(databaseHelper_);

    int64_t outPersonId = -1;
    try {
        if (!personHelper.LookupPersonIdBySessionToken(transaction, token, outPersonId)) {
            return false;
        }
    }
    catch (const std::exception&) {
        return false;
    }

    // Double-check session status via Sessions helper (revoked/expired already checked by PersonHelper,
    // but ensure row exists).
    TableHelpers::Sessions sessions(databaseHelper_);
    KeyValueTable row;
    try {
        row = sessions.LookupSessionByUuid(transaction, token);
    } catch (...) {
        return false;
    }
    if (row.empty()) {
        return false;
    }
    // If we needed extra checks (revoked/expired) PersonHelper already did those.

    personId_ = outPersonId;
    return true;
}

bool Session::InitializeFromDeviceToken(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    CookieManagerPtr cookieManager) {
    if (!cookieManager) {
        return false;
    }
    auto cookies = cookieManager->GetCookies();
    auto it = cookies.find("device_token");
    if (it == cookies.end()) {
        return false;
    }
    const std::string& cookie = it->second;

    // Parse cookie value into tokenId and base64EncodedSecret
    auto [tokenId, base64EncodedSecret] = SplitString(cookie, ".");

    if(tokenId.empty() || base64EncodedSecret.empty()) {
        return false;
    }

    std::string outTokenId;
    std::string outBase64EncodedSecret;

    Auth::PersonHelper personHelper(databaseHelper_);
    if (!personHelper.TryLoginWithDeviceToken(
        transaction,
        secrets,
        tokenId,
        base64EncodedSecret,
        outTokenId,
        outBase64EncodedSecret)) {
        return false;
    }

    // Lookup person id associated with device token
    TableHelpers::DeviceTokens deviceTokens(databaseHelper_);
    KeyValueTable row = deviceTokens.LookupDeviceTokenByUuid(transaction, outTokenId);
    if (row.empty()) {
        return false;
    }

    const std::string& personIdStr = row.at(std::string(DbSchema::kDeviceTokensPersonId));
    int64_t personId = -1;
    try {
        personId = std::stoll(personIdStr);
    }
    catch (...) {
        return false;
    }

    PersonInfo personInfo;
    if (!personHelper.LookupPerson(transaction, personId, personInfo)) {
        return false;
    }

    // Create session token
    std::string token;
    if (!personHelper.CreateSessionToken(
        transaction, secrets, personInfo.email, token)) {
        return false;
    }

    // Set remember to false since device token already exists
    InitializeFromLogin(
        transaction, secrets, token, cookieManager, personInfo.email, false);

    SetDeviceTokenCookie(
        transaction, secrets, outTokenId, outBase64EncodedSecret, cookieManager);
    return true;
}

void Session::SetDeviceTokenCookie(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string_view tokenId,
    std::string_view base64EncodedSecret,
    CookieManagerPtr cookieManager) {
    int64_t maxAgeInMicros = -1;
    std::string maxAgeStr = secrets->LookupSecret(
        transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros);
    if (!maxAgeStr.empty()) {
        try {
            maxAgeInMicros = std::stoll(maxAgeStr);
        }
        catch (...) {
            maxAgeInMicros = -1;
        }
    }
    std::string value = std::string(tokenId) + "." + std::string(base64EncodedSecret);
    SetCookieHelper(
        transaction,
        secrets,
        "device_token",
        value,
        maxAgeInMicros,
        cookieManager);
}

void Session::SetCsrfTokenCookie(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    int64_t maxAgeMicros,
    CookieManagerPtr cookieManager) {
    // 32 bytes of randomness, base64-url-encoded — same shape as the
    // session uuid the rest of the auth code uses. Two cookies set
    // back-to-back from independent RNG draws would also be fine
    // (CSRF tokens don't need to be tied to the session), but
    // generating fresh material on every login keeps the cookie
    // jar clean and avoids any temptation to reuse stale tokens.
    AuthHelper authHelper;
    Blob raw = authHelper.RandomBytes(32);
    csrfToken_ = authHelper.Base64Encode(raw);

    // Phase 6.1: build attributes via the shared helper so the
    // set-time and clear-time (logout.cpp) attribute lists are
    // literally the same code. The whole point of the double-
    // submit pattern is that the SPA can read this value and echo
    // it as an X-CSRF-Token header — that's why httpOnly is false
    // here, while the session_token / device_token cookies remain
    // HttpOnly.
    bool isProd = ServerConfig::GetInstance().IsProdMode();
    CookieProperties props = BuildAuthCookieProperties(
        transaction, secrets, isProd,
        /*httpOnly=*/false, maxAgeMicros);
    cookieManager->SetCookie("csrft", csrfToken_, props);
}

void Session::SetCookieHelper(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string_view key,
    std::string_view value,
    int64_t maxAgeMicros,
    CookieManagerPtr cookieManager) {
    // Phase 6.1: shared canonical attribute factory.
    bool isProd = ServerConfig::GetInstance().IsProdMode();
    CookieProperties props = BuildAuthCookieProperties(
        transaction, secrets, isProd,
        /*httpOnly=*/true, maxAgeMicros);
    cookieManager->SetCookie(key, value, props);
}

bool Session::IsAdmin(Transaction& transaction) {
    return ActiveUserHasRole(transaction, DbSchema::kRoleNameAdmin);
}

// Role/permission checks with transaction parameter
bool Session::ActiveUserHasRole(Transaction& transaction, std::string_view roleName) {
    if (!IsLoggedIn()) {
        throw std::runtime_error("Session::ActiveUserHasRole - not logged in");
    }

    TableHelpers::Roles roles(databaseHelper_);
    KeyValueTable roleRow = roles.GetRole(transaction, roleName);
    int64_t roleId = std::stoll(roleRow.at(std::string(DbSchema::kRolesId)));

    TableHelpers::RoleAssignments ra(databaseHelper_);
    KeyValueTableArray assignments = ra.GetRoleAssignmentsForPerson(transaction, personId_);

    for (const auto& kv : assignments) {
        int64_t rid = std::stoll(kv.at(std::string(DbSchema::kRoleAssignmentsRoleId)));
        if (rid == roleId) {
            return true;
        }
    }
    return false;
}

bool Session::ActiveUserHasPermission(Transaction& transaction, std::string_view permissionName) {
    if (!IsLoggedIn()) {
        throw std::runtime_error("Session::ActiveUserHasPermission - not logged in");
    }

    // Resolve permission id by name
    TableHelpers::Permissions perms(databaseHelper_);
    KeyValueTable pRow = perms.GetPermission(transaction, permissionName);
    int64_t targetPermId = std::stoll(pRow.at(std::string(DbSchema::kPermissionsId)));

    // Fetch role assignments for user
    TableHelpers::RoleAssignments ra(databaseHelper_);
    KeyValueTableArray assignments = ra.GetRoleAssignmentsForPerson(transaction, personId_);

    // For each role, check role_permissions for the target permission
    TableHelpers::RolePermissions rp(databaseHelper_);
    for (const auto& kv : assignments) {
        int64_t roleId = std::stoll(kv.at(std::string(DbSchema::kRoleAssignmentsRoleId)));
        KeyValueTableArray rpRows = rp.GetRolePermissionsForRole(transaction, roleId);
        for (const auto& rpKv : rpRows) {
            int64_t permId = std::stoll(rpKv.at(std::string(DbSchema::kRolePermissionsPermissionId)));
            if (permId == targetPermId) {
                return true;
            }
        }
    }
    return false;
}

} // namespace Auth