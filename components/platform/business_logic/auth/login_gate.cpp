#include "login_gate.h"

#include <stdexcept>

#include "db_schema/login_attempts.h"
#include "db_schema/people.h"
#include "sql_util/table_helpers/login_attempts.h"
#include "util/secrets/secret_keys.h"

namespace Auth {

namespace {

// Defensive defaults. If the secrets table is missing a row (e.g.
// fresh deploy that hasn't run PopulateConfigSecrets yet), the gate
// still has sensible behavior. These mirror the production values
// in secret_values.cpp; out-of-sync drift is fine since secrets
// override.
constexpr int64_t kDefaultLoginMaxFailuresPerEmail = 10;
constexpr int64_t kDefaultLoginMaxFailuresPerIp = 50;
constexpr int64_t kDefaultLoginFailureWindowMicros = 900LL * 1000000LL;  // 15 min
constexpr int64_t kDefaultVerifyMaxFailuresPerIp = 30;
constexpr int64_t kDefaultVerifyFailureWindowMicros = 900LL * 1000000LL;
constexpr int64_t kDefaultRememberMaxFailuresPerIp = 50;
constexpr int64_t kDefaultRememberFailureWindowMicros = 900LL * 1000000LL;

}  // namespace

LoginGate::LoginGate(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

int64_t LoginGate::LookupInt64Secret(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string_view key,
    int64_t fallback) {
    if (!secrets) return fallback;
    std::string val = secrets->LookupSecret(transaction, key);
    if (val.empty()) return fallback;
    try {
        return std::stoll(val);
    } catch (...) {
        return fallback;
    }
}

LoginGateResult LoginGate::CheckBeforeLogin(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string_view email,
    std::string_view ip) {

    // Account-level lockout — single SELECT against people. Reads
    // locked_until and compares against now_us(). When the account
    // doesn't exist, the row count is 0 and we fall through to the
    // failure-count checks (which on an unknown email also count
    // zero, by design — we don't enumerate accounts).
    if (!email.empty()) {
        std::string lockedUntilStr = transaction.RunSqlStatementReturningOneValue(
            "SELECT COALESCE(MAX(locked_until), 0) FROM people "
            "WHERE email = $1",
            std::string(email));
        int64_t lockedUntil = 0;
        try {
            lockedUntil = std::stoll(lockedUntilStr);
        } catch (...) {
            lockedUntil = 0;
        }
        if (lockedUntil > 0) {
            std::string nowStr = transaction.RunSqlStatementReturningOneValue(
                "SELECT now_us()");
            int64_t nowUs = 0;
            try {
                nowUs = std::stoll(nowStr);
            } catch (...) {
                nowUs = 0;
            }
            if (lockedUntil > nowUs) {
                return LoginGateResult::AccountLocked;
            }
        }
    }

    int64_t windowMicros = LookupInt64Secret(
        transaction, secrets,
        Secrets::kAuthLoginFailureWindowInMicros,
        kDefaultLoginFailureWindowMicros);
    int64_t emailThreshold = LookupInt64Secret(
        transaction, secrets,
        Secrets::kAuthLoginMaxFailuresPerEmailPerWindow,
        kDefaultLoginMaxFailuresPerEmail);
    int64_t ipThreshold = LookupInt64Secret(
        transaction, secrets,
        Secrets::kAuthLoginMaxFailuresPerIpPerWindow,
        kDefaultLoginMaxFailuresPerIp);

    TableHelpers::LoginAttempts attempts(databaseHelper_);

    // Per-email check (only when we have an email — empty input
    // skips, since there's no bucket to compare against).
    if (!email.empty()) {
        int64_t emailFailures = attempts.RecentFailureCountForEmail(
            transaction, email,
            DbSchema::kLoginAttemptsKindLogin,
            windowMicros);
        if (emailFailures >= emailThreshold) {
            return LoginGateResult::RateLimitedEmail;
        }
    }

    // Per-IP check. Skip when the resolver couldn't determine an IP
    // (running locally without a proxy, etc.) — the gate should not
    // false-positive on requests where the IP plumbing is incomplete.
    if (!ip.empty()) {
        int64_t ipFailures = attempts.RecentFailureCountForIp(
            transaction, ip,
            DbSchema::kLoginAttemptsKindLogin,
            windowMicros);
        if (ipFailures >= ipThreshold) {
            return LoginGateResult::RateLimitedIp;
        }
    }

    return LoginGateResult::Allow;
}

LoginGateResult LoginGate::CheckBeforeVerify(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string_view ip) {
    if (ip.empty()) return LoginGateResult::Allow;

    int64_t windowMicros = LookupInt64Secret(
        transaction, secrets,
        Secrets::kAuthVerifyFailureWindowInMicros,
        kDefaultVerifyFailureWindowMicros);
    int64_t ipThreshold = LookupInt64Secret(
        transaction, secrets,
        Secrets::kAuthVerifyMaxFailuresPerIpPerWindow,
        kDefaultVerifyMaxFailuresPerIp);

    TableHelpers::LoginAttempts attempts(databaseHelper_);
    int64_t ipFailures = attempts.RecentFailureCountForIp(
        transaction, ip,
        DbSchema::kLoginAttemptsKindVerify,
        windowMicros);
    if (ipFailures >= ipThreshold) {
        return LoginGateResult::RateLimitedIp;
    }
    return LoginGateResult::Allow;
}

LoginGateResult LoginGate::CheckBeforeRemember(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string_view ip) {
    if (ip.empty()) return LoginGateResult::Allow;

    int64_t windowMicros = LookupInt64Secret(
        transaction, secrets,
        Secrets::kAuthRememberFailureWindowInMicros,
        kDefaultRememberFailureWindowMicros);
    int64_t ipThreshold = LookupInt64Secret(
        transaction, secrets,
        Secrets::kAuthRememberMaxFailuresPerIpPerWindow,
        kDefaultRememberMaxFailuresPerIp);

    TableHelpers::LoginAttempts attempts(databaseHelper_);
    int64_t ipFailures = attempts.RecentFailureCountForIp(
        transaction, ip,
        DbSchema::kLoginAttemptsKindRemember,
        windowMicros);
    if (ipFailures >= ipThreshold) {
        return LoginGateResult::RateLimitedIp;
    }
    return LoginGateResult::Allow;
}

}  // namespace Auth
