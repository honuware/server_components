#include "server_config.h"

#include <cstdlib>
#include <string>
#include <vector>

#include <crow/middlewares/cors.h>

#include "endpoints/web_app.h"  // full WebApp definition (GetApp())
#include "business_logic/auth/proxy_trust.h"
#include "util/secrets/secrets_at_rest.h"
#include "util/env.h"
#include "util/logging.h"

namespace Auth {

namespace {

// Phase 12.1: small helper that returns true iff the env var is set
// AND non-empty. Mirrors the pattern used in cloudfront_origin_guard
// and proxy_trust — "set but empty" is operationally identical to
// "unset" and the validator must treat them the same way (otherwise
// `export HONUWARE_ORIGIN_SECRET=""` would silently disable the
// guard). Checks the canonical HONUWARE_* name first, then the legacy
// KNOTTYYOGA_* fallback (componentization Phase 1.1).
bool EnvVarSetNonEmpty(const char* primary, const char* fallback) {
    const char* val = Util::GetEnvWithFallback(primary, fallback);
    return val != nullptr && val[0] != '\0';
}

// Phase 7.2 of the security review: dev-mode CORS opt-in. When this
// env var is set to a non-empty origin (e.g. "http://localhost:4200"),
// dev-mode startup registers that origin with CORS so a developer
// running the SPA on a different port than the API can talk to it
// directly without the Angular proxy. When unset (default), dev mode
// stays no-CORS — matching the long-standing same-origin-via-proxy
// pattern documented in ui/proxy.conf.json.
//
// In prod mode this var is ignored — prod always pins to
// kWebsiteAddress regardless of the env.
//
// Canonical HONUWARE_* name; legacy KNOTTYYOGA_* honored as a fallback during
// the componentization rename (Phase 1.1).
constexpr const char* kDevCorsOriginEnvVar = "HONUWARE_DEV_CORS_ORIGIN";
constexpr const char* kDevCorsOriginEnvVarLegacy = "KNOTTYYOGA_DEV_CORS_ORIGIN";

std::string ReadDevCorsOrigin() {
    const char* val = Util::GetEnvWithFallback(
        kDevCorsOriginEnvVar, kDevCorsOriginEnvVarLegacy);
    if (val == nullptr || val[0] == '\0') return "";
    return std::string(val);
}

}  // namespace

std::unique_ptr<ServerConfig> ServerConfig::s_instance;

ServerConfig& ServerConfig::GetInstance() {
    if (!s_instance) {
        throw std::runtime_error("ServerConfig::GetInstance - not initialized");
    }
    return *s_instance;
}

ServerConfig& ServerConfig::GetInstancePrivate() {
    if (s_instance) {
        throw std::runtime_error("ServerConfig::GetInstancePrivate - already initialized");
    }
    s_instance = std::unique_ptr<ServerConfig>(new ServerConfig());
    return *s_instance;
}

void ServerConfig::Initialize(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secretsHelper,
    WebApp* webApp) {
    ServerConfig& inst = GetInstancePrivate();
    if (!secretsHelper) {
        throw std::runtime_error("ServerConfig::Initialize - secretsHelper null");
    }
    std::string value = secretsHelper->LookupSecret(transaction, Secrets::kServerProductionMode);
    inst.prodMode_ = StringToBool(value);
    inst.testMode_ = false;

    // --- CORS config ---
    std::string serverName = secretsHelper->LookupSecret(
        transaction, Secrets::kWebsiteAddress);
    auto& cors = webApp->GetApp().get_middleware<crow::CORSHandler>();
    if (inst.prodMode_) {
        cors
            .global()
            .origin(serverName)  // your SPA origin
            .methods("GET"_method, "POST"_method, "OPTIONS"_method)
            // Phase 7.2 / Phase 4.3 of the security review: include
            // X-CSRF-Token in the allow-list so the SPA's CSRF
            // interceptor can attach it on cross-origin POSTs.
            .headers("Content-Type", "Authorization", "X-CSRF-Token")
            .allow_credentials();               // Access-Control-Allow-Credentials: true
    }
    else {
        // Phase 7.2 of the security review: dev-mode CORS opt-in via
        // KNOTTYYOGA_DEV_CORS_ORIGIN. Default behavior is unchanged
        // (no-CORS — Angular proxy keeps everything same-origin).
        // When the env var is set, register that exact origin with
        // credentials and the same methods/headers as prod, so a dev
        // workflow that doesn't use the Angular proxy still works.
        std::string devOrigin = ReadDevCorsOrigin();
        if (!devOrigin.empty()) {
            LogInfo() << "[server_config] event=dev_cors_enabled origin="
                      << devOrigin << "\n";
            cors
                .global()
                .origin(devOrigin)
                .methods("GET"_method, "POST"_method, "OPTIONS"_method)
                .headers("Content-Type", "Authorization", "X-CSRF-Token")
                .allow_credentials();
        }
    }
}

void ServerConfig::InitializeTestMode() {
    ServerConfig& inst = GetInstancePrivate();
    inst.prodMode_ = false;
    inst.testMode_ = true;
}

void ServerConfig::ValidateProdEnvironment(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secretsHelper) {
    ServerConfig& inst = GetInstance();
    if (!inst.prodMode_) {
        // Dev or test mode — the prod guards are advisory, not
        // load-bearing, so absent env vars are fine. Skip silently.
        return;
    }

    std::vector<std::string> missing;

    if (!EnvVarSetNonEmpty("HONUWARE_ORIGIN_SECRET", "KNOTTYYOGA_ORIGIN_SECRET")) {
        // Without this set, CloudFrontOriginGuard short-circuits and
        // accepts requests that didn't traverse CloudFront. In prod
        // that means anything hitting the EC2 IP directly bypasses
        // the WAF / CDN protections.
        missing.emplace_back(
            "HONUWARE_ORIGIN_SECRET (env var) — required so the "
            "CloudFront origin guard can reject requests that bypass "
            "the CDN");
    }
    // ProxyTrustEnabled returns true iff the value is "1" or "true"
    // (case-insensitive — see proxy_trust.cpp). An unset or "0"
    // value silently keeps ResolveClientIp on the connection peer,
    // which behind CloudFront is always CloudFront's IP — Phase 5.5
    // rate-limit buckets would collapse to one bucket for the
    // entire internet. Refuse to boot.
    if (!ProxyTrustEnabled()) {
        missing.emplace_back(
            "HONUWARE_TRUST_PROXY (env var) — required so "
            "ResolveClientIp honors X-Forwarded-For behind CloudFront; "
            "without it every request looks like it came from "
            "CloudFront's IP and the per-IP rate limit collapses");
    }
    if (!EnvVarSetNonEmpty(Secrets::kSecretKeyEnvVar, Secrets::kSecretKeyEnvVarLegacy)) {
        // Phase 8.1 already fails loud inside SecretsAtRest::Make
        // when the key is missing in prod, but we re-check here so
        // an operator sees the full guard list in one error message
        // rather than discovering missing env vars one at a time
        // across multiple redeploy attempts.
        missing.emplace_back(
            "HONUWARE_SECRET_KEY (env var) — required for at-rest "
            "encryption of config_secrets values (Phase 8); without "
            "it the server falls back to plaintext storage");
    }

    // kWebsiteAddress drives the prod CORS pin AND the verification-
    // email link base URL. Empty means CORS effectively rejects all
    // browser requests AND verification emails go out with broken
    // links.
    std::string websiteAddress =
        secretsHelper
            ? secretsHelper->LookupSecret(transaction, Secrets::kWebsiteAddress)
            : "";
    if (websiteAddress.empty()) {
        missing.emplace_back(
            "config_secrets.website_address (DB secret) — required "
            "for the prod CORS origin pin and for verification-email "
            "links to resolve to the SPA");
    }

    if (missing.empty()) return;

    std::string detail =
        "ServerConfig::ValidateProdEnvironment - production mode is "
        "enabled but the following required configuration is missing:\n";
    for (const auto& item : missing) {
        detail += "  - " + item + "\n";
    }
    detail +=
        "Fix the missing items in the deploy environment (ECS task "
        "definition / systemd EnvironmentFile / etc.) and redeploy. "
        "Refusing to start so the server doesn't run half-protected.";
    LogError() << "[server_config] event=prod_validation_failed missing="
               << missing.size() << "\n"
               << detail << "\n";
    throw std::runtime_error(detail);
}

void ServerConfig::Shutdown() {
    s_instance.reset();
}

std::string ServerConfig::BuildSecurityPostureSummary() {
    ServerConfig& inst = GetInstance();
    auto flag = [](bool set) { return set ? "set" : "unset"; };
    std::string summary = "[security_posture] prod=";
    summary += (inst.prodMode_ ? "true" : "false");
    summary += " csrf_guard=on";
    summary += " origin_secret=";
    summary += flag(EnvVarSetNonEmpty(
        "HONUWARE_ORIGIN_SECRET", "KNOTTYYOGA_ORIGIN_SECRET"));
    summary += " proxy_trust=";
    // We deliberately report "set" for proxy_trust only when the
    // value is a recognized truthy ("1"/"true") — anything else,
    // including "0"/"false"/unset, is the same effective state
    // (proxy untrusted) so they should display the same.
    summary += flag(ProxyTrustEnabled());
    summary += " secret_key=";
    summary += flag(EnvVarSetNonEmpty(Secrets::kSecretKeyEnvVar, Secrets::kSecretKeyEnvVarLegacy));
    summary += " security_headers=on";
    return summary;
}

bool ServerConfig::IsProdMode() const {
    return prodMode_;
}

bool ServerConfig::IsTestMode() const {
    return testMode_;
}

} // namespace Auth