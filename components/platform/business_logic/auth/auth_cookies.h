#pragma once

#include <cstdint>

#include "cookie_manager.h"
#include "sql_util/database_access/transaction.h"
#include "util/secrets/secrets_helper.h"

namespace Auth {

// Phase 6.1 of the security review: canonical attributes for the
// three auth-related cookies (session_token, device_token, csrft).
//
// Why this exists: previously, session.cpp built CookieProperties at
// set-time and logout.cpp built them ad hoc at clear-time, with
// subtly different values (SameSite=None at clear, Lax at set).
// Browsers refuse to delete a cookie when the Set-Cookie header that
// "expires" it disagrees with the original on Path/Domain/SameSite,
// so the logout was leaving stale auth cookies in the browser.
// Centralising the attributes here makes set-and-clear use literally
// the same code path.
//
// `httpOnly` is the only attribute that differs between the three
// cookies — session_token and device_token are HttpOnly (so script
// can't read the session token); csrft is NOT (so the SPA can read
// it via document.cookie and echo it as the X-CSRF-Token header).
// Caller passes the right flag for the cookie they're building.
//
// In production mode the cookies get `Secure` + `Domain` set from
// the website-address secret. In test/dev mode they don't, so a
// dev environment over plain HTTP works.
CookieProperties BuildAuthCookieProperties(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    bool isProd,
    bool httpOnly,
    int64_t maxAgeMicros);

// Phase 6.1 of the security review: clears all three auth cookies
// (session_token, device_token, csrft) with attributes that mirror
// the set-time attributes in session.cpp. Each is cleared with
// Max-Age=0 so the browser drops it immediately. Called from
// logout.cpp.
//
// Reads `kServerProductionMode` / `kWebsiteAddress` from `secrets`
// (via ServerConfig + the same path session.cpp uses) to decide
// whether to set Secure + Domain — matching the set-time decision.
void ClearAuthCookies(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    CookieManagerPtr cookieManager);

}  // namespace Auth
