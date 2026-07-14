#include "auth_cookies.h"

#include "server_config.h"
#include "util/secrets/secret_keys.h"

namespace Auth {

CookieProperties BuildAuthCookieProperties(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    bool isProd,
    bool httpOnly,
    int64_t maxAgeMicros) {
    CookieProperties props;
    props.path = "/";
    props.httpOnly = httpOnly;
    // SameSite=Lax across the board — the AWS deploy is same-origin
    // behind CloudFront, so the browser never sends a cross-site
    // cookie and we don't need the SameSite=None; Secure dance.
    // Phase 6.1 specifically calls out dropping SameSite=None at
    // clear-time (which logout.cpp used to use) so set/clear
    // attributes agree — browsers won't apply a deletion when the
    // Set-Cookie header disagrees with the live cookie on these
    // fields.
    props.sameSite = CookieSameSitePolicy::Lax;
    props.maxAgeInMicros = maxAgeMicros;

    if (isProd && secrets) {
        props.secure = true;
        props.domain = secrets->LookupSecret(transaction, Secrets::kWebsiteAddress);
    } else {
        props.secure = false;
    }
    return props;
}

void ClearAuthCookies(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    CookieManagerPtr cookieManager) {
    if (!cookieManager) return;

    bool isProd = ServerConfig::GetInstance().IsProdMode();

    // Max-Age = 0 expires the cookie immediately. The set-time path
    // uses a positive Max-Age; everything else (Path, SameSite,
    // Secure, Domain) must match.
    CookieProperties httpOnlyProps = BuildAuthCookieProperties(
        transaction, secrets, isProd, /*httpOnly=*/true,
        /*maxAgeMicros=*/0);
    cookieManager->SetCookie("session_token", "", httpOnlyProps);
    cookieManager->SetCookie("device_token", "", httpOnlyProps);

    // csrft is the SPA-readable companion (Phase 4.1) — its set-time
    // attributes use HttpOnly=false, so the clear-time call has to
    // mirror that.
    CookieProperties csrfProps = BuildAuthCookieProperties(
        transaction, secrets, isProd, /*httpOnly=*/false,
        /*maxAgeMicros=*/0);
    cookieManager->SetCookie("csrft", "", csrfProps);
}

}  // namespace Auth
