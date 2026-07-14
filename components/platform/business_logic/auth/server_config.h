#pragma once

#include <memory>
#include <stdexcept>
#include <string>

#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper.h"
#include "sql_util/database_common.h" // Transaction
#include "util/types.h"
#include "endpoints/web_app_types.h"  // forward-declares WebApp (see header)

namespace Auth {

class ServerConfig {
public:
    ServerConfig(const ServerConfig&) = delete;
    ServerConfig& operator=(const ServerConfig&) = delete;
    ~ServerConfig() = default;

    // Access the singleton instance (throws if not initialized).
    static ServerConfig& GetInstance();

    // Initialize from production/dev mode secrets.
    static void Initialize(
        Transaction& transaction, 
        Secrets::SecretsHelperPtr secretsHelper, 
        WebApp* webApp);

    // Initialize for test mode (no secrets needed).
    static void InitializeTestMode();

    // Destroy the singleton.
    static void Shutdown();

    // Phase 12.1 of the security review: fail loud at startup when
    // production-required configuration is missing. Called from
    // `main.cpp` immediately after `Initialize`. No-op in dev/test
    // mode — `Initialize` set the mode flag, and we only enforce
    // the prod guards when `IsProdMode()` is true.
    //
    // Checks (in prod mode). Env vars use the canonical HONUWARE_* names,
    // with the legacy KNOTTYYOGA_* names still honored as a fallback
    // (componentization Phase 1.1):
    //   - env var `HONUWARE_ORIGIN_SECRET` is set and non-empty
    //     (Phase 1.7 — CloudFront origin guard)
    //   - env var `HONUWARE_TRUST_PROXY` is set to a truthy value
    //     (Phase 5.5 — `ResolveClientIp` correctness behind CloudFront)
    //   - env var `HONUWARE_SECRET_KEY` is set and non-empty
    //     (Phase 8 — at-rest secrets encryption master key)
    //   - secret `kWebsiteAddress` is set and non-empty
    //     (Phase 7.2 — CORS origin / verify-link base URL)
    //
    // Throws `std::runtime_error` listing ALL missing items so an
    // operator can fix every problem in one redeploy instead of
    // playing whack-a-mole. `main.cpp` lets the throw propagate so
    // the process exits non-zero — ECS / systemd then surface the
    // misconfiguration immediately rather than letting the server
    // boot half-protected.
    static void ValidateProdEnvironment(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secretsHelper);

    // Phase 12.2 of the security review: one-line startup summary
    // of the server's security posture, suitable for logging at
    // boot. Format:
    //   "[security_posture] prod=<bool> csrf_guard=on
    //    origin_secret=<set|unset> proxy_trust=<set|unset>
    //    secret_key=<set|unset> security_headers=on"
    //
    // The `=on` flags (csrf_guard, security_headers) are unconditional
    // because they're middleware that's always wired (Phases 4 and
    // 7). The `<set|unset>` flags reflect process env-var state —
    // they tell an operator at a glance whether each Phase 1.7 /
    // 5.5 / 8 guard is actually configured for this run.
    //
    // Pure function: no logging side effects, no env mutation.
    // main.cpp calls this and pipes the result to LogInfo so test
    // assertions can pin the format string without a log-capture
    // facility.
    static std::string BuildSecurityPostureSummary();

    bool IsProdMode() const;
    bool IsTestMode() const;

private:
    ServerConfig() = default;

    // Creates the instance if not present; throws if already initialized.
    static ServerConfig& GetInstancePrivate();

    static std::unique_ptr<ServerConfig> s_instance;

    bool prodMode_ = false;
    bool testMode_ = false;
};

} // namespace Auth