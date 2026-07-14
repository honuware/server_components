#pragma once

#include <string>
#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "util/secrets/secrets_helper.h"

namespace Auth {

// Phase 5.2 of the security review.
//
// LoginGate is the synchronous read-side of the rate-limit / lockout
// machinery. It runs BEFORE Argon2id on every login (and before the
// equivalent auth check for /api/verify and /api/remember) so a
// flooding attack stops at the gate, not at the password hasher.
//
// The gate's decision is read-only — it never updates state. The
// post-attempt write-behind path (Phase 5.4) records the outcome
// async via ThreadPool, and the lockout decision (Phase 5.3) lands
// in `people.locked_until` synchronously alongside the password
// verify.
//
// Three thresholds for login (all configurable as secrets):
//   - account-level: people.locked_until > now()  → AccountLocked
//   - per-email:     RecentFailureCountForEmail >= threshold  → RateLimitedEmail
//   - per-IP:        RecentFailureCountForIp    >= threshold  → RateLimitedIp
//
// Verify / remember are per-IP only — those flows don't have a
// confirmed email tied to the request.
//
// All four "denied" results turn into the same generic 429 to the
// client (Phase 5.7); the distinct enum values exist for logging /
// metrics inside the server.
enum class LoginGateResult {
    Allow,
    AccountLocked,
    RateLimitedEmail,
    RateLimitedIp,
};

class LoginGate {
public:
    LoginGate(DatabaseHelper databaseHelper);
    LoginGate(const LoginGate&) = default;
    LoginGate& operator=(const LoginGate&) = default;
    ~LoginGate() = default;

    // Pre-flight check for /api/login. Reads people.locked_until,
    // per-email failure count, per-IP failure count; returns the
    // FIRST failing reason in that order. Email may be the
    // unverified value the user submitted — if no row matches,
    // the locked_until / per-email checks short-circuit (no row =>
    // not locked => zero failures).
    LoginGateResult CheckBeforeLogin(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        std::string_view email,
        std::string_view ip);

    // Pre-flight check for /api/verify. Per-IP only; verify doesn't
    // have a confirmed email.
    LoginGateResult CheckBeforeVerify(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        std::string_view ip);

    // Pre-flight check for /api/remember. Per-IP only.
    LoginGateResult CheckBeforeRemember(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        std::string_view ip);

private:
    DatabaseHelper databaseHelper_;

    // Helper: read a numeric secret with a fallback default. The
    // gate is conservative — when a secret is missing or unparseable,
    // it falls back to the default rather than failing open.
    int64_t LookupInt64Secret(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        std::string_view key,
        int64_t fallback);
};

}  // namespace Auth
