#include "password_reset_helper.h"

#include <regex>
#include <string>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "business_logic/auth/auth_helper.h"
#include "business_logic/auth/login_gate.h"
#include "business_logic/auth/person.h"
#include "db_schema/password_reset_tokens.h"
#include "db_schema/people.h"
#include "sql_util/table_helpers/password_reset_tokens.h"
#include "sql_util/table_helpers/people.h"
#include "test/src/util/database_test_helper.h"
#include "util/mail/mail_helper_test_util.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/thread_pool.h"

// Finishing website Phase 11.3. Every outcome is read back through the code
// that uses it in production — PersonHelper::VerifyPassword for the password,
// LookupPersonIdBySessionToken / TryLoginWithDeviceToken for sign-outs,
// LoginGate for the lockout, IsPerson for verification, and CompleteReset
// itself for whether a link still works.

namespace Auth {
namespace {

using ::Mail::Test::MakeTestMailHelper;
using ::Mail::Test::TestMailHelperPtr;
using ::testing::HasSubstr;

constexpr std::string_view kOldPassword = "old-password";
constexpr std::string_view kNewPassword = "new-password";

// Explicit branding, so the framework-only build (no app defaults) and the app
// build (Knotty Yoga's) read the same values.
Secrets::Test::SecretsHelperTestPtr MakeSecrets() {
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    secrets->AddSecretTest(Secrets::kMailSenderName, "Test Studio");
    secrets->AddSecretTest(Secrets::kMailSenderAddress, "studio@example.com");
    secrets->AddSecretTest(Secrets::kWebsiteAddressLogin, "https://example.com/");
    return secrets;
}

int64_t AddVerifiedPerson(
    Transaction& tx, TestDatabaseUtil& testDb, const std::string& email) {
    PersonHelper personHelper(testDb.GetDatabaseHelper());
    personHelper.CreateFullyValidatedUser(
        tx, PersonInfo{ email, "Alice", "Smith" }, kOldPassword);
    TableHelpers::People people(testDb.GetDatabaseHelper());
    return std::stoll(people.LookupPersonByEmail(tx, email)
                          .at(std::string(DbSchema::kPeopleId)));
}

// Requests a reset, flushes the queued send, and returns the secret from the
// link in the one mail it produced.
std::string RequestAndReadSecret(
    Transaction& tx, TestDatabaseUtil& testDb,
    Secrets::SecretsHelperPtr secrets, std::string_view email) {
    TestMailHelperPtr mail = MakeTestMailHelper();
    PasswordResetHelper helper(testDb.GetDatabaseHelper());
    EXPECT_GT(helper.RequestReset(tx, secrets, mail, email), 0);
    ThreadPool::GetInstance().Shutdown();
    EXPECT_EQ(mail->GetMessages().size(), 1u);
    if (mail->GetMessages().empty()) {
        return {};
    }
    std::smatch match;
    const std::string body = mail->GetMessages()[0].GetBodyHtml();
    EXPECT_TRUE(std::regex_search(body, match, std::regex("secret=([A-Za-z0-9_-]+)")));
    return match.size() > 1 ? match[1].str() : std::string();
}

PasswordResetOutcome Complete(
    Transaction& tx, TestDatabaseUtil& testDb, Secrets::SecretsHelperPtr secrets,
    std::string_view email, std::string_view secret,
    std::string_view newPassword = kNewPassword) {
    PasswordResetHelper helper(testDb.GetDatabaseHelper());
    return helper.CompleteReset(tx, secrets, email, secret, newPassword).outcome;
}

bool PasswordIs(
    Transaction& tx, TestDatabaseUtil& testDb,
    std::string_view email, std::string_view password) {
    PersonHelper personHelper(testDb.GetDatabaseHelper());
    return personHelper.VerifyPassword(tx, email, password);
}

// What session restore asks of a session cookie. A deleted session throws
// ("not found") rather than answering false; either way it signs nobody in.
bool SessionSignsIn(Transaction& tx, TestDatabaseUtil& testDb, const std::string& token) {
    PersonHelper personHelper(testDb.GetDatabaseHelper());
    int64_t personId = 0;
    try {
        return personHelper.LookupPersonIdBySessionToken(tx, token, personId);
    } catch (const std::exception&) {
        return false;
    }
}

// ---------- RequestReset ----------

TEST(PasswordResetHelperTest, AnUnknownEmailStoresAndSendsNothing) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetUnknownEmail", [&](Transaction& tx) {
        TestMailHelperPtr mail = MakeTestMailHelper();
        PasswordResetHelper helper(testDb.GetDatabaseHelper());

        EXPECT_EQ(helper.RequestReset(tx, MakeSecrets(), mail, "nobody@example.com"), 0);
        ThreadPool::GetInstance().Shutdown();

        EXPECT_TRUE(mail->GetMessages().empty());
        EXPECT_EQ(tx.RunSqlStatementReturningOneValue(
                      "SELECT count(*) FROM password_reset_tokens"),
                  "0");
    });
}

TEST(PasswordResetHelperTest, AKnownEmailGetsOneMailWithTheLink) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetKnownEmail", [&](Transaction& tx) {
        int64_t personId = AddVerifiedPerson(tx, testDb, "alice@example.com");
        TestMailHelperPtr mail = MakeTestMailHelper();
        PasswordResetHelper helper(testDb.GetDatabaseHelper());

        EXPECT_EQ(helper.RequestReset(tx, MakeSecrets(), mail, "alice@example.com"),
                  personId);
        ThreadPool::GetInstance().Shutdown();

        ASSERT_EQ(mail->GetMessages().size(), 1u);
        const ::Mail::MailMessage& message = mail->GetMessages()[0];
        EXPECT_EQ(message.GetSubject(), "Reset your Test Studio password");
        EXPECT_EQ(message.GetFrom().name, "Test Studio");
        EXPECT_EQ(message.GetFrom().address, "studio@example.com");
        ASSERT_EQ(message.GetTo().size(), 1u);
        EXPECT_EQ(message.GetTo()[0].address, "alice@example.com");
        EXPECT_EQ(message.GetTo()[0].name, "Alice Smith");
        EXPECT_THAT(message.GetBodyHtml(), HasSubstr(
            "https://example.com/reset-password?email=alice%40example.com&secret="));
        EXPECT_THAT(message.GetBodyHtml(), HasSubstr("expires in 60 minutes"));
    });
}

TEST(PasswordResetHelperTest, OnlyAHashOfTheSecretIsStored) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetStoresHash", [&](Transaction& tx) {
        int64_t personId = AddVerifiedPerson(tx, testDb, "alice@example.com");
        const std::string secret =
            RequestAndReadSecret(tx, testDb, MakeSecrets(), "alice@example.com");
        // 32 random bytes as unpadded URL-safe base64.
        EXPECT_EQ(secret.size(), 43u);

        TableHelpers::PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        const std::string stored = tokens.GetForPerson(tx, personId)
            .at(std::string(DbSchema::kPasswordResetTokensTokenHash));
        EXPECT_NE(stored, secret);
        EXPECT_EQ(stored.find(secret), std::string::npos);
        AuthHelper auth;
        EXPECT_EQ(stored, auth.HashBinary(auth.Base64Decode(secret)));
    });
}

TEST(PasswordResetHelperTest, TheLinkLastsTheConfiguredMinutes) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetTtl", [&](Transaction& tx) {
        int64_t personId = AddVerifiedPerson(tx, testDb, "alice@example.com");
        auto secrets = MakeSecrets();
        secrets->AddSecretTest(Secrets::kAuthPasswordResetTtlMinutes, "15");
        TestMailHelperPtr mail = MakeTestMailHelper();
        PasswordResetHelper helper(testDb.GetDatabaseHelper());

        const int64_t before = std::stoll(tx.RunSqlStatementReturningOneValue("SELECT now_us()"));
        helper.RequestReset(tx, secrets, mail, "alice@example.com");
        const int64_t after = std::stoll(tx.RunSqlStatementReturningOneValue("SELECT now_us()"));
        ThreadPool::GetInstance().Shutdown();

        TableHelpers::PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        const int64_t expiresAt = std::stoll(tokens.GetForPerson(tx, personId)
            .at(std::string(DbSchema::kPasswordResetTokensExpiresAt)));
        const int64_t fifteenMinutesUs = 15LL * 60LL * 1000000LL;
        EXPECT_GE(expiresAt, before + fifteenMinutesUs);
        EXPECT_LE(expiresAt, after + fifteenMinutesUs);
        ASSERT_EQ(mail->GetMessages().size(), 1u);
        EXPECT_THAT(mail->GetMessages()[0].GetBodyHtml(), HasSubstr("expires in 15 minutes"));
    });
}

TEST(PasswordResetHelperTest, TheMailGoesToTheStoredAddress) {
    // people.email is CITEXT, so "ALICE@Example.com" finds the account; the
    // mail and its link use the address as it was registered.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetStoredAddress", [&](Transaction& tx) {
        AddVerifiedPerson(tx, testDb, "alice@example.com");
        TestMailHelperPtr mail = MakeTestMailHelper();
        PasswordResetHelper helper(testDb.GetDatabaseHelper());

        EXPECT_GT(helper.RequestReset(tx, MakeSecrets(), mail, "ALICE@Example.com"), 0);
        ThreadPool::GetInstance().Shutdown();

        ASSERT_EQ(mail->GetMessages().size(), 1u);
        EXPECT_EQ(mail->GetMessages()[0].GetTo()[0].address, "alice@example.com");
        EXPECT_THAT(mail->GetMessages()[0].GetBodyHtml(),
                    HasSubstr("?email=alice%40example.com&"));
    });
}

TEST(PasswordResetHelperTest, ASecondRequestKillsTheFirstLink) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetSecondRequest", [&](Transaction& tx) {
        AddVerifiedPerson(tx, testDb, "alice@example.com");
        auto secrets = MakeSecrets();
        const std::string first =
            RequestAndReadSecret(tx, testDb, secrets, "alice@example.com");
        const std::string second =
            RequestAndReadSecret(tx, testDb, secrets, "alice@example.com");
        ASSERT_NE(first, second);

        EXPECT_EQ(Complete(tx, testDb, secrets, "alice@example.com", first),
                  PasswordResetOutcome::InvalidLink);
        EXPECT_EQ(Complete(tx, testDb, secrets, "alice@example.com", second),
                  PasswordResetOutcome::Success);
    });
}

// ---------- CompleteReset ----------

TEST(PasswordResetHelperTest, SuccessChangesThePassword) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetChangesPassword", [&](Transaction& tx) {
        int64_t personId = AddVerifiedPerson(tx, testDb, "alice@example.com");
        auto secrets = MakeSecrets();
        const std::string secret =
            RequestAndReadSecret(tx, testDb, secrets, "alice@example.com");

        PasswordResetHelper helper(testDb.GetDatabaseHelper());
        PasswordResetResult result = helper.CompleteReset(
            tx, secrets, "alice@example.com", secret, kNewPassword);

        EXPECT_EQ(result.outcome, PasswordResetOutcome::Success);
        EXPECT_EQ(result.personId, personId);
        EXPECT_FALSE(PasswordIs(tx, testDb, "alice@example.com", kOldPassword));
        EXPECT_TRUE(PasswordIs(tx, testDb, "alice@example.com", kNewPassword));
    });
}

TEST(PasswordResetHelperTest, SuccessUsesTheProductionHashCost) {
    // The secrets decide the Argon2id cost; the stored hash says what it was.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetHashCost", [&](Transaction& tx) {
        AddVerifiedPerson(tx, testDb, "alice@example.com");
        auto secrets = MakeSecrets();
        secrets->AddSecretTest(Secrets::kAuthArgon2OpsLimit, "3");
        secrets->AddSecretTest(Secrets::kAuthArgon2MemLimitKb, "65536");
        const std::string secret =
            RequestAndReadSecret(tx, testDb, secrets, "alice@example.com");

        ASSERT_EQ(Complete(tx, testDb, secrets, "alice@example.com", secret),
                  PasswordResetOutcome::Success);

        TableHelpers::People people(testDb.GetDatabaseHelper());
        const std::string hash = people.LookupPersonByEmail(tx, "alice@example.com")
            .at(std::string(DbSchema::kPeoplePasswordHash));
        EXPECT_THAT(hash, HasSubstr("m=65536,t=3,"));
    });
}

TEST(PasswordResetHelperTest, TheLinkWorksOnce) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetOnce", [&](Transaction& tx) {
        AddVerifiedPerson(tx, testDb, "alice@example.com");
        auto secrets = MakeSecrets();
        const std::string secret =
            RequestAndReadSecret(tx, testDb, secrets, "alice@example.com");

        ASSERT_EQ(Complete(tx, testDb, secrets, "alice@example.com", secret),
                  PasswordResetOutcome::Success);
        EXPECT_EQ(Complete(tx, testDb, secrets, "alice@example.com", secret,
                           "third-password"),
                  PasswordResetOutcome::InvalidLink);
        EXPECT_TRUE(PasswordIs(tx, testDb, "alice@example.com", kNewPassword));
    });
}

TEST(PasswordResetHelperTest, AnExpiredLinkFails) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetExpired", [&](Transaction& tx) {
        int64_t personId = AddVerifiedPerson(tx, testDb, "alice@example.com");
        auto secrets = MakeSecrets();
        const std::string secret =
            RequestAndReadSecret(tx, testDb, secrets, "alice@example.com");
        // An hour (and a microsecond) later.
        tx.RunSqlStatement(
            "UPDATE password_reset_tokens SET expires_at = now_us() - 1 "
            "WHERE person_id = $1",
            std::to_string(personId));

        EXPECT_EQ(Complete(tx, testDb, secrets, "alice@example.com", secret),
                  PasswordResetOutcome::InvalidLink);
        EXPECT_TRUE(PasswordIs(tx, testDb, "alice@example.com", kOldPassword));
    });
}

TEST(PasswordResetHelperTest, AWrongSecretFailsAndCounts) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetWrongSecret", [&](Transaction& tx) {
        int64_t personId = AddVerifiedPerson(tx, testDb, "alice@example.com");
        auto secrets = MakeSecrets();
        const std::string secret =
            RequestAndReadSecret(tx, testDb, secrets, "alice@example.com");
        AuthHelper auth;
        const std::string wrong = auth.Base64Encode(auth.RandomBytes(32));

        EXPECT_EQ(Complete(tx, testDb, secrets, "alice@example.com", wrong),
                  PasswordResetOutcome::InvalidLink);
        // Not even base64.
        EXPECT_EQ(Complete(tx, testDb, secrets, "alice@example.com", "not base64!"),
                  PasswordResetOutcome::InvalidLink);

        TableHelpers::PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        EXPECT_EQ(tokens.GetForPerson(tx, personId)
                      .at(std::string(DbSchema::kPasswordResetTokensAttempts)),
                  "2");
        EXPECT_TRUE(PasswordIs(tx, testDb, "alice@example.com", kOldPassword));
        // The real link still works after a couple of misses.
        EXPECT_EQ(Complete(tx, testDb, secrets, "alice@example.com", secret),
                  PasswordResetOutcome::Success);
    });
}

TEST(PasswordResetHelperTest, TheFifthWrongSecretDeletesTheLink) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetFifthWrong", [&](Transaction& tx) {
        int64_t personId = AddVerifiedPerson(tx, testDb, "alice@example.com");
        auto secrets = MakeSecrets();
        const std::string secret =
            RequestAndReadSecret(tx, testDb, secrets, "alice@example.com");
        TableHelpers::PasswordResetTokens tokens(testDb.GetDatabaseHelper());

        for (int i = 0; i < 4; ++i) {
            EXPECT_EQ(Complete(tx, testDb, secrets, "alice@example.com", "wrong"),
                      PasswordResetOutcome::InvalidLink);
        }
        EXPECT_FALSE(tokens.GetForPerson(tx, personId).empty());
        EXPECT_EQ(Complete(tx, testDb, secrets, "alice@example.com", "wrong"),
                  PasswordResetOutcome::InvalidLink);

        EXPECT_TRUE(tokens.GetForPerson(tx, personId).empty());
        EXPECT_EQ(Complete(tx, testDb, secrets, "alice@example.com", secret),
                  PasswordResetOutcome::InvalidLink);
        EXPECT_TRUE(PasswordIs(tx, testDb, "alice@example.com", kOldPassword));
    });
}

TEST(PasswordResetHelperTest, AnUnknownEmailFails) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetUnknownComplete", [&](Transaction& tx) {
        EXPECT_EQ(Complete(tx, testDb, MakeSecrets(), "nobody@example.com", "anything"),
                  PasswordResetOutcome::InvalidLink);
    });
}

TEST(PasswordResetHelperTest, AnEmptyPasswordIsRejectedWithoutSpendingATry) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetEmptyPassword", [&](Transaction& tx) {
        int64_t personId = AddVerifiedPerson(tx, testDb, "alice@example.com");
        auto secrets = MakeSecrets();
        const std::string secret =
            RequestAndReadSecret(tx, testDb, secrets, "alice@example.com");

        EXPECT_EQ(Complete(tx, testDb, secrets, "alice@example.com", secret, ""),
                  PasswordResetOutcome::PasswordRejected);

        TableHelpers::PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        EXPECT_EQ(tokens.GetForPerson(tx, personId)
                      .at(std::string(DbSchema::kPasswordResetTokensAttempts)),
                  "0");
        EXPECT_TRUE(PasswordIs(tx, testDb, "alice@example.com", kOldPassword));
    });
}

TEST(PasswordResetHelperTest, SuccessClearsMustChangeAndTheLockout) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetClearsFlags", [&](Transaction& tx) {
        int64_t personId = AddVerifiedPerson(tx, testDb, "alice@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.SetMustChangePassword(tx, personId, true);
        tx.RunSqlStatement(
            "UPDATE people SET failed_login_attempts = 10, "
            "locked_until = now_us() + 3600000000 WHERE id = $1",
            std::to_string(personId));
        auto secrets = MakeSecrets();
        LoginGate gate(testDb.GetDatabaseHelper());
        ASSERT_EQ(gate.CheckBeforeLogin(tx, secrets, "alice@example.com", ""),
                  LoginGateResult::AccountLocked);

        const std::string secret =
            RequestAndReadSecret(tx, testDb, secrets, "alice@example.com");
        ASSERT_EQ(Complete(tx, testDb, secrets, "alice@example.com", secret),
                  PasswordResetOutcome::Success);

        EXPECT_EQ(gate.CheckBeforeLogin(tx, secrets, "alice@example.com", ""),
                  LoginGateResult::Allow);
        KeyValueTable person = people.GetPersonById(tx, personId);
        EXPECT_EQ(person.at(std::string(DbSchema::kPeopleMustChangePassword)), "f");
        EXPECT_EQ(person.at(std::string(DbSchema::kPeopleFailedLoginAttempts)), "0");
        PersonHelper personHelper(testDb.GetDatabaseHelper());
        EXPECT_TRUE(personHelper.LoginPerson(
            tx, secrets, "alice@example.com", kNewPassword).success);
    });
}

TEST(PasswordResetHelperTest, SuccessEndsEverySessionAndDeviceToken) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetEndsSessions", [&](Transaction& tx) {
        AddVerifiedPerson(tx, testDb, "alice@example.com");
        auto secrets = MakeSecrets();
        PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string session;
        ASSERT_TRUE(personHelper.CreateSessionToken(tx, secrets, "alice@example.com", session));
        std::string deviceTokenId;
        std::string deviceSecret;
        ASSERT_TRUE(personHelper.CreateDeviceToken(
            tx, secrets, "alice@example.com", deviceTokenId, deviceSecret));
        ASSERT_TRUE(SessionSignsIn(tx, testDb, session));
        // Remember-me works before the reset. Using it rotates the token, so
        // the rotated one is what the browser would hold afterwards.
        std::string heldId;
        std::string heldSecret;
        ASSERT_TRUE(personHelper.TryLoginWithDeviceToken(
            tx, secrets, deviceTokenId, deviceSecret, heldId, heldSecret));

        const std::string secret =
            RequestAndReadSecret(tx, testDb, secrets, "alice@example.com");
        ASSERT_EQ(Complete(tx, testDb, secrets, "alice@example.com", secret),
                  PasswordResetOutcome::Success);

        EXPECT_FALSE(SessionSignsIn(tx, testDb, session));
        std::string rotatedId;
        std::string rotatedSecret;
        EXPECT_FALSE(personHelper.TryLoginWithDeviceToken(
            tx, secrets, heldId, heldSecret, rotatedId, rotatedSecret));
    });
}

TEST(PasswordResetHelperTest, SuccessVerifiesAnUnverifiedAccount) {
    // An account whose activation link expired unclicked: the reset link
    // proves the inbox, so it is verified and can sign in.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetVerifies", [&](Transaction& tx) {
        auto secrets = MakeSecrets();
        PersonHelper personHelper(testDb.GetDatabaseHelper());
        personHelper.PreliminaryRegisterPerson(
            tx, secrets, MakeTestMailHelper(),
            PersonInfo{ "new@example.com", "Nora", "New" }, kOldPassword,
            /*sendEmail=*/false);
        ASSERT_FALSE(personHelper.IsPerson(tx, "new@example.com"));

        const std::string secret =
            RequestAndReadSecret(tx, testDb, secrets, "new@example.com");
        ASSERT_EQ(Complete(tx, testDb, secrets, "new@example.com", secret),
                  PasswordResetOutcome::Success);

        EXPECT_TRUE(personHelper.IsPerson(tx, "new@example.com"));
        EXPECT_TRUE(PasswordIs(tx, testDb, "new@example.com", kNewPassword));
    });
}

TEST(PasswordResetHelperTest, APendingVerificationAndAResetCoexist) {
    // Separate tables: asking for a reset leaves an unclicked activation
    // link alone (and the reset, once done, makes it moot).
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetCoexists", [&](Transaction& tx) {
        auto secrets = MakeSecrets();
        PersonHelper personHelper(testDb.GetDatabaseHelper());
        personHelper.PreliminaryRegisterPerson(
            tx, secrets, MakeTestMailHelper(),
            PersonInfo{ "both@example.com", "Bo", "Th" }, kOldPassword,
            /*sendEmail=*/false);

        RequestAndReadSecret(tx, testDb, secrets, "both@example.com");

        EXPECT_EQ(tx.RunSqlStatementReturningOneValue(
                      "SELECT count(*) FROM email_verifications v "
                      "JOIN people p ON p.id = v.person_id WHERE p.email = $1",
                      std::string("both@example.com")),
                  "1");
    });
}

TEST(PasswordResetHelperTest, ANullMailStoresTheLinkAndSendsNothing) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetNullMail", [&](Transaction& tx) {
        int64_t personId = AddVerifiedPerson(tx, testDb, "alice@example.com");
        PasswordResetHelper helper(testDb.GetDatabaseHelper());
        EXPECT_EQ(helper.RequestReset(tx, MakeSecrets(), nullptr, "alice@example.com"),
                  personId);
        ThreadPool::GetInstance().Shutdown();
        TableHelpers::PasswordResetTokens tokens(testDb.GetDatabaseHelper());
        EXPECT_FALSE(tokens.GetForPerson(tx, personId).empty());
    });
}

}  // namespace
}  // namespace Auth
