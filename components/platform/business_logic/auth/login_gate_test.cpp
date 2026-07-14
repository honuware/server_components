#include "login_gate.h"

#include <gtest/gtest.h>

#include "db_schema/login_attempts.h"
#include "sql_util/table_helpers/login_attempts.h"
#include "test/src/util/database_test_helper.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"

// Phase 5.2 of the security review.
//
// LoginGate has three entry points (login / verify / remember). The
// login path consults locked_until + per-email + per-IP; verify and
// remember are per-IP only.
//
// Each test uses a small (e.g. 3) threshold so we can exercise the
// "at threshold" boundary without inserting thousands of rows. The
// real production thresholds (10+) are handled by integration in
// person_test.cpp and verify_test.cpp.

namespace Auth {
namespace {

using ::TableHelpers::LoginAttempts;

// Insert N failed-login rows for (email, ip) under the requested
// kind. Uses the table helper so the rows look exactly like
// production rows, with attempted_at = now_us().
void InsertFailures(
    Transaction& tx,
    DatabaseHelper db,
    int n,
    std::string_view email,
    std::string_view ip,
    std::string_view kind = DbSchema::kLoginAttemptsKindLogin) {
    LoginAttempts attempts(db);
    for (int i = 0; i < n; ++i) {
        attempts.RecordAttempt(tx, email, ip, kind, /*success=*/false);
    }
}

// Add the secret values the gate reads. Caller picks tight
// thresholds so the test exercises the boundary.
void SeedLoginGateSecrets(
    Transaction& tx,
    Secrets::Test::SecretsHelperTestPtr secrets,
    int64_t emailThreshold,
    int64_t ipThreshold,
    int64_t windowMicros = 60LL * 1000000LL) {
    secrets->AddSecret(tx,
        Secrets::kAuthLoginMaxFailuresPerEmailPerWindow,
        std::to_string(emailThreshold));
    secrets->AddSecret(tx,
        Secrets::kAuthLoginMaxFailuresPerIpPerWindow,
        std::to_string(ipThreshold));
    secrets->AddSecret(tx,
        Secrets::kAuthLoginFailureWindowInMicros,
        std::to_string(windowMicros));
}

// ---------- CheckBeforeLogin ----------

TEST(LoginGateTest, AllowWhenNoFailures) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AllowWhenNoFailures", [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedLoginGateSecrets(tx, secrets, 3, 5);

        LoginGate gate(testDb.GetDatabaseHelper());
        EXPECT_EQ(gate.CheckBeforeLogin(tx, secrets, "u@example.com", "1.1.1.1"),
                  LoginGateResult::Allow);
    });
}

TEST(LoginGateTest, RateLimitedEmailAtThreshold) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RateLimitedEmailAtThreshold", [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedLoginGateSecrets(tx, secrets, /*emailThreshold=*/3, /*ipThreshold=*/100);

        // Insert exactly threshold failures from a SINGLE email but
        // many IPs (so per-email trips first).
        for (int i = 0; i < 3; ++i) {
            InsertFailures(tx, testDb.GetDatabaseHelper(), 1,
                "victim@example.com",
                "10.0.0." + std::to_string(i));
        }

        LoginGate gate(testDb.GetDatabaseHelper());
        EXPECT_EQ(gate.CheckBeforeLogin(
                      tx, secrets, "victim@example.com", "203.0.113.7"),
                  LoginGateResult::RateLimitedEmail);
    });
}

TEST(LoginGateTest, RateLimitedIpAtThreshold) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RateLimitedIpAtThreshold", [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedLoginGateSecrets(tx, secrets, /*emailThreshold=*/100, /*ipThreshold=*/3);

        // 3 failures from one IP across many emails — the
        // attacker-spreading-across-emails case the per-IP cap
        // catches.
        for (int i = 0; i < 3; ++i) {
            InsertFailures(tx, testDb.GetDatabaseHelper(), 1,
                "v" + std::to_string(i) + "@example.com",
                "10.0.0.1");
        }

        LoginGate gate(testDb.GetDatabaseHelper());
        EXPECT_EQ(gate.CheckBeforeLogin(
                      tx, secrets, "fresh@example.com", "10.0.0.1"),
                  LoginGateResult::RateLimitedIp);
    });
}

TEST(LoginGateTest, EmailLimitTakesPrecedenceOverIpLimit) {
    // When both per-email AND per-IP would be RateLimited, the gate
    // returns Email (the design doc orders email before IP because
    // email is the more specific signal — telling the operator the
    // gate caught a credential stuffing vs a generic flood).
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "EmailLimitTakesPrecedenceOverIpLimit", [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedLoginGateSecrets(tx, secrets, /*emailThreshold=*/2, /*ipThreshold=*/2);

        InsertFailures(tx, testDb.GetDatabaseHelper(), 5,
            "victim@example.com", "10.0.0.1");

        LoginGate gate(testDb.GetDatabaseHelper());
        EXPECT_EQ(gate.CheckBeforeLogin(
                      tx, secrets, "victim@example.com", "10.0.0.1"),
                  LoginGateResult::RateLimitedEmail);
    });
}

TEST(LoginGateTest, AccountLockedTakesPrecedenceOverFailureCounts) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "AccountLockedTakesPrecedenceOverFailureCounts", [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedLoginGateSecrets(tx, secrets, 3, 5);

        // Create a person and lock them in the future. now_us() +
        // 1 hour should outlast any test.
        testDb.AddPerson(tx, "locked@example.com", "F", "L", "h");
        std::string nowUs = tx.RunSqlStatementReturningOneValue("SELECT now_us()");
        int64_t futureLockUntil = std::stoll(nowUs) + 3600LL * 1000000LL;
        tx.RunSqlStatement(
            "UPDATE people SET locked_until = $1 WHERE email = $2",
            std::to_string(futureLockUntil),
            std::string("locked@example.com"));

        // Even with no failures recorded, the gate returns AccountLocked.
        LoginGate gate(testDb.GetDatabaseHelper());
        EXPECT_EQ(gate.CheckBeforeLogin(
                      tx, secrets, "locked@example.com", "10.0.0.1"),
                  LoginGateResult::AccountLocked);
    });
}

TEST(LoginGateTest, ExpiredAccountLockoutIsIgnored) {
    // locked_until in the PAST means the lock has already lifted —
    // the gate must NOT count it as locked.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ExpiredAccountLockoutIsIgnored", [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedLoginGateSecrets(tx, secrets, 3, 5);

        testDb.AddPerson(tx, "expired@example.com", "F", "L", "h");
        // 1 hour ago.
        std::string nowUs = tx.RunSqlStatementReturningOneValue("SELECT now_us()");
        int64_t pastLockUntil = std::stoll(nowUs) - 3600LL * 1000000LL;
        tx.RunSqlStatement(
            "UPDATE people SET locked_until = $1 WHERE email = $2",
            std::to_string(pastLockUntil),
            std::string("expired@example.com"));

        LoginGate gate(testDb.GetDatabaseHelper());
        EXPECT_EQ(gate.CheckBeforeLogin(
                      tx, secrets, "expired@example.com", "10.0.0.1"),
                  LoginGateResult::Allow);
    });
}

TEST(LoginGateTest, UnknownEmailFallsThroughToIpCheck) {
    // Must NOT short-circuit on the locked_until / per-email lookup
    // when the email doesn't match a row. An attacker spraying
    // login attempts at fake@example.com would otherwise bypass the
    // per-IP gate.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "UnknownEmailFallsThroughToIpCheck", [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedLoginGateSecrets(tx, secrets, /*emailThreshold=*/100, /*ipThreshold=*/3);

        // 3 failures from one IP across DIFFERENT random emails.
        for (int i = 0; i < 3; ++i) {
            InsertFailures(tx, testDb.GetDatabaseHelper(), 1,
                "spray" + std::to_string(i) + "@example.com",
                "10.0.0.1");
        }

        LoginGate gate(testDb.GetDatabaseHelper());
        // Even the (never-before-seen) email gets the per-IP block.
        EXPECT_EQ(gate.CheckBeforeLogin(
                      tx, secrets, "totally-new@example.com", "10.0.0.1"),
                  LoginGateResult::RateLimitedIp);
    });
}

TEST(LoginGateTest, MissingSecretsFallBackToDefaults) {
    // No secrets seeded — the gate should fall back to its compiled
    // defaults, not throw. Defaults are loose enough that 1 failure
    // doesn't trip them.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("MissingSecretsFallBackToDefaults", [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        // Intentionally NO SeedLoginGateSecrets() call.

        InsertFailures(tx, testDb.GetDatabaseHelper(), 1,
            "u@example.com", "10.0.0.1");

        LoginGate gate(testDb.GetDatabaseHelper());
        EXPECT_EQ(gate.CheckBeforeLogin(
                      tx, secrets, "u@example.com", "10.0.0.1"),
                  LoginGateResult::Allow);
    });
}

TEST(LoginGateTest, EmptyIpSkipsPerIpCheck) {
    // When the IP plumbing can't determine a client IP (running
    // locally without a proxy), the gate must not false-positive.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EmptyIpSkipsPerIpCheck", [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedLoginGateSecrets(tx, secrets, 100, 1);

        InsertFailures(tx, testDb.GetDatabaseHelper(), 5,
            "u@example.com", "10.0.0.1");

        LoginGate gate(testDb.GetDatabaseHelper());
        EXPECT_EQ(gate.CheckBeforeLogin(
                      tx, secrets, "u@example.com", /*ip=*/""),
                  LoginGateResult::Allow);
    });
}

// ---------- CheckBeforeVerify ----------

TEST(LoginGateTest, VerifyAllowWhenUnderThreshold) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyAllowWhenUnderThreshold", [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(tx,
            Secrets::kAuthVerifyMaxFailuresPerIpPerWindow, "5");
        secrets->AddSecret(tx,
            Secrets::kAuthVerifyFailureWindowInMicros,
            std::to_string(60LL * 1000000LL));

        InsertFailures(tx, testDb.GetDatabaseHelper(), 2,
            "n/a", "10.0.0.1",
            DbSchema::kLoginAttemptsKindVerify);

        LoginGate gate(testDb.GetDatabaseHelper());
        EXPECT_EQ(gate.CheckBeforeVerify(tx, secrets, "10.0.0.1"),
                  LoginGateResult::Allow);
    });
}

TEST(LoginGateTest, VerifyRateLimitedAtThreshold) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyRateLimitedAtThreshold", [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(tx,
            Secrets::kAuthVerifyMaxFailuresPerIpPerWindow, "3");
        secrets->AddSecret(tx,
            Secrets::kAuthVerifyFailureWindowInMicros,
            std::to_string(60LL * 1000000LL));

        InsertFailures(tx, testDb.GetDatabaseHelper(), 3,
            "n/a", "10.0.0.1",
            DbSchema::kLoginAttemptsKindVerify);

        LoginGate gate(testDb.GetDatabaseHelper());
        EXPECT_EQ(gate.CheckBeforeVerify(tx, secrets, "10.0.0.1"),
                  LoginGateResult::RateLimitedIp);
    });
}

TEST(LoginGateTest, VerifyIsScopedToVerifyKindOnly) {
    // Login failures must NOT bleed into the verify gate, and
    // vice-versa — separate buckets.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyIsScopedToVerifyKindOnly", [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(tx,
            Secrets::kAuthVerifyMaxFailuresPerIpPerWindow, "2");
        secrets->AddSecret(tx,
            Secrets::kAuthVerifyFailureWindowInMicros,
            std::to_string(60LL * 1000000LL));

        InsertFailures(tx, testDb.GetDatabaseHelper(), 5,
            "n/a", "10.0.0.1",
            DbSchema::kLoginAttemptsKindLogin);

        LoginGate gate(testDb.GetDatabaseHelper());
        EXPECT_EQ(gate.CheckBeforeVerify(tx, secrets, "10.0.0.1"),
                  LoginGateResult::Allow);
    });
}

// ---------- CheckBeforeRemember ----------

TEST(LoginGateTest, RememberRateLimitedAtThreshold) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RememberRateLimitedAtThreshold", [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(tx,
            Secrets::kAuthRememberMaxFailuresPerIpPerWindow, "2");
        secrets->AddSecret(tx,
            Secrets::kAuthRememberFailureWindowInMicros,
            std::to_string(60LL * 1000000LL));

        InsertFailures(tx, testDb.GetDatabaseHelper(), 2,
            "n/a", "10.0.0.1",
            DbSchema::kLoginAttemptsKindRemember);

        LoginGate gate(testDb.GetDatabaseHelper());
        EXPECT_EQ(gate.CheckBeforeRemember(tx, secrets, "10.0.0.1"),
                  LoginGateResult::RateLimitedIp);
    });
}

TEST(LoginGateTest, RememberAllowsEmptyIp) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RememberAllowsEmptyIp", [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        LoginGate gate(testDb.GetDatabaseHelper());
        EXPECT_EQ(gate.CheckBeforeRemember(tx, secrets, /*ip=*/""),
                  LoginGateResult::Allow);
    });
}

}  // namespace
}  // namespace Auth
