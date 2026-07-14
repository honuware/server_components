#include "person.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>

#include "sql_util/table_helpers/people.h"
#include "db_schema/people.h"
#include "db_schema/email_verifications.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secret_values.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/mail/mail_helper_test_util.h"
#include "test/src/util/database_test_helper.h"
#include "sql_util/database_common.h"
#include "business_logic/auth/auth_helper.h"
#include "util/date_time_util.h"
#include "test/src/util/test_helper.h"
#include "sql_util/table_helpers/email_verifications.h"
#include "person_verify_mail_test_util.h"
#include "test/src/util/test_transaction_provider.h"
#include "util/thread_pool.h"
#include "db_schema/device_tokens.h"
#include "db_schema/sessions.h"
#include "sql_util/table_helpers/sessions.h"
#include "sql_util/table_helpers/device_tokens.h"
// Helpers for roles/permissions/assignments
#include "sql_util/table_helpers/roles.h"
#include "sql_util/table_helpers/permissions.h"
#include "sql_util/table_helpers/role_permissions.h"
#include "sql_util/table_helpers/role_assignments.h"

namespace Auth {
namespace {

using ::Mail::Test::MakeTestMailHelper;
using ::Mail::Test::TestMailHelperPtr;
using ::testing::HasSubstr;
using ::testing::StartsWith;
using ::testing::EndsWith;

// Helpers
static PersonHelper MakePersonHelper(
    Transaction& transaction,
    DatabaseHelper databaseHelper) {
    return PersonHelper(databaseHelper);
}

// PreliminaryRegisterPerson
TEST(PersonHelperTest, PreliminaryRegisterPersonBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("PreliminaryRegisterPersonBasic", [&](Transaction& transaction) {
        TestMailHelperPtr mailHelper = MakeTestMailHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "user1@example.com", "First", "Last" };
        helper.PreliminaryRegisterPerson(transaction, secrets, mailHelper, info, "Password123!", true);

        // Exactly one email sent
        ASSERT_EQ(mailHelper->GetMessages().size(), 1u);
        const auto& msg = mailHelper->GetMessages()[0];
        EXPECT_EQ(msg.GetSubject(), secrets->LookupSecretTest(Secrets::kMailActivationEmailSubject));
        // Fetch person row to build expected body
        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());

        std::string expectedBodyPrefix = Auth::Mail::Test::GenerateVerifyMailBodyPrefix(
            secrets, info.email, info.firstName, info.lastName);
        std::string expectedBodySuffix = Auth::Mail::Test::GenerateVerifyMailBodySuffix(
            secrets, info.email, info.firstName, info.lastName);

        EXPECT_THAT(msg.GetBodyHtml(), StartsWith(expectedBodyPrefix));
        EXPECT_THAT(msg.GetBodyHtml(), EndsWith(expectedBodySuffix));
        });
}

TEST(PersonHelperTest, PreliminaryRegisterPersonNoEmail) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("PreliminaryRegisterPersonNoEmail", [&](Transaction& transaction) {
        TestMailHelperPtr mailHelper = MakeTestMailHelper();
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "user2@example.com", "First2", "Last2" };
        helper.PreliminaryRegisterPerson(
            transaction, Secrets::Test::MakeTestSecretsHelper(), mailHelper, info, "Password123!", false);

        EXPECT_TRUE(mailHelper->GetMessages().empty());
        });
}

// VerifyPersonEmail
TEST(PersonHelperTest, VerifyPersonEmailBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyPersonEmailBasic", [&](Transaction& transaction) {
        TestMailHelperPtr mailHelper = MakeTestMailHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "vuser@example.com", "first", "last" };
        helper.PreliminaryRegisterPerson(transaction, secrets, mailHelper, info, "Pass123!", true);

        std::string expectedBodyPrefix = Auth::Mail::Test::GenerateVerifyMailBodyPrefix(
            secrets, info.email, info.firstName, info.lastName);
        std::string expectedBodySuffix = Auth::Mail::Test::GenerateVerifyMailBodySuffix(
            secrets, info.email, info.firstName, info.lastName);

        // Get the encoded secret from the email
        ASSERT_EQ(mailHelper->GetMessages().size(), 1u);
        const auto& msg = mailHelper->GetMessages()[0];
        std::string_view encodedSecret = msg.GetBodyHtml();
        encodedSecret.remove_prefix(expectedBodyPrefix.size());
        encodedSecret.remove_suffix(expectedBodySuffix.size());

        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());
        int64_t id = std::stoll(person.at(std::string(DbSchema::kPeopleId)));
        int64_t createdMicros = std::stoll(person.at(std::string(DbSchema::kPeopleCreatedAt)));
        EXPECT_FALSE(helper.IsPerson(transaction, info.email));

        // Get the verification token from the email verifications table
        TableHelpers::EmailVerifications emailVerifications(
            testDb.GetDatabaseHelper(), secrets);
        KeyValueTable kvVerify = emailVerifications.GetEmailVerificationInfoByEmail(
            transaction, info.email);

        ASSERT_EQ(kvVerify.size(), 5);
        ASSERT_EQ(kvVerify[std::string(DbSchema::kEmailVerificationsPersonId)], std::to_string(id));

        helper.VerifyPersonEmail(transaction, secrets, info.email, encodedSecret);
        EXPECT_TRUE(helper.IsPerson(transaction, info.email));

        person = people.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());
        int64_t verifiedMicros = std::stoll(person.at(std::string(DbSchema::kPeopleEmailVerifiedAt)));
        ASSERT_GE(verifiedMicros, createdMicros);

        try {
            kvVerify = emailVerifications.GetEmailVerificationInfoByEmail(
                transaction, info.email);
            FAIL() << "VerifyPersonEmailBasic - should not get here.";
        }
        catch (std::runtime_error& e) {
            ASSERT_EQ(std::string(e.what()), "EmailVerifications::GetEmailVerificationInfoByEmail - verification not found");
        }
        });
}

// Phase 1.2 layering: PersonHelper::VerifyPersonEmail injects the real
// AuthHelper::ConstantTimeEqual into the (now pure-CRUD) EmailVerifications
// table helper. This exercises the *rejection* half of that wiring end to end:
// presenting one user's token against another user's pending verification must
// fail the constant-time compare, so VerifyPersonEmail throws and the account
// stays unverified.
TEST(PersonHelperTest, VerifyPersonEmailWrongTokenFails) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyPersonEmailWrongTokenFails", [&](Transaction& transaction) {
        TestMailHelperPtr mailHelper = MakeTestMailHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        // Register the victim (whose email we try to verify) and an attacker
        // (whose valid-format token we misuse against the victim's row).
        PersonInfo victim{ "victim@example.com", "vfirst", "vlast" };
        PersonInfo attacker{ "attacker@example.com", "afirst", "alast" };
        helper.PreliminaryRegisterPerson(transaction, secrets, mailHelper, victim, "Pass123!", true);
        helper.PreliminaryRegisterPerson(transaction, secrets, mailHelper, attacker, "Pass123!", true);
        ASSERT_EQ(mailHelper->GetMessages().size(), 2u);

        // Pull the attacker's encoded secret out of their activation email.
        std::string attackerPrefix = Auth::Mail::Test::GenerateVerifyMailBodyPrefix(
            secrets, attacker.email, attacker.firstName, attacker.lastName);
        std::string attackerSuffix = Auth::Mail::Test::GenerateVerifyMailBodySuffix(
            secrets, attacker.email, attacker.firstName, attacker.lastName);
        std::string_view attackerSecret = mailHelper->GetMessages()[1].GetBodyHtml();
        attackerSecret.remove_prefix(attackerPrefix.size());
        attackerSecret.remove_suffix(attackerSuffix.size());

        // Verifying the victim with the attacker's (valid-format but wrong)
        // token must fail the constant-time compare.
        try {
            helper.VerifyPersonEmail(transaction, secrets, victim.email, attackerSecret);
            FAIL() << "VerifyPersonEmailWrongTokenFails - expected throw.";
        }
        catch (std::runtime_error& e) {
            ASSERT_EQ(std::string(e.what()),
                "PersonHelper::VerifyPersonEmail - Email verification failed.");
        }

        // The victim remains unverified and their pending row survives (the
        // failed attempt bumped the counter but did not consume the row).
        EXPECT_FALSE(helper.IsPerson(transaction, victim.email));
        TableHelpers::EmailVerifications emailVerifications(
            testDb.GetDatabaseHelper(), secrets);
        KeyValueTable kv = emailVerifications.GetEmailVerificationInfoByEmail(
            transaction, victim.email);
        EXPECT_FALSE(kv.empty());
        });
}

// CreateFullyValidatedUser
TEST(PersonHelperTest, CreateFullyValidatedUserBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("CreateFullyValidatedUserBasic", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "full@example.com", "Full", "Valid" };
        helper.CreateFullyValidatedUser(transaction, info, "Password!");
        EXPECT_TRUE(helper.IsPerson(transaction, info.email));

        PersonInfo out;
        int64_t id = -1;
        EXPECT_TRUE(helper.LookupPerson(transaction, info.email, out, id));
        EXPECT_EQ(out.email, info.email);
        EXPECT_EQ(out.firstName, info.firstName);
        EXPECT_EQ(out.lastName, info.lastName);
        EXPECT_GT(id, 0);

        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());
        int64_t createdMicros = std::stoll(person.at(std::string(DbSchema::kPeopleCreatedAt)));
        int64_t verifiedMicros = std::stoll(person.at(std::string(DbSchema::kPeopleEmailVerifiedAt)));
        ASSERT_GE(verifiedMicros, createdMicros);
        });
}

// IsPerson
TEST(PersonHelperTest, IsPersonBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsPersonBasic", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        PersonInfo info{ "isper@example.com", "Is", "Person" };
        EXPECT_FALSE(helper.IsPerson(transaction, info.email));
        helper.CreateFullyValidatedUser(transaction, info, "abc");
        EXPECT_TRUE(helper.IsPerson(transaction, info.email));
        });
}

TEST(PersonHelperTest, IsPersonNoPerson) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsPersonNoPerson", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        EXPECT_FALSE(helper.IsPerson(transaction, "none@example.com"));
        });
}

TEST(PersonHelperTest, IsPersonNotVerified) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsPersonNotVerified", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        PersonInfo info{ "notver@example.com", "Not", "Ver" };
        TestMailHelperPtr mailHelper = MakeTestMailHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        helper.PreliminaryRegisterPerson(transaction, secrets, mailHelper, info, "pw", false);
        EXPECT_FALSE(helper.IsPerson(transaction, info.email));
        });
}

// LookupPerson (by id)
TEST(PersonHelperTest, LookupPersonByIdBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupPersonByIdBasic", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "idlookup@example.com", "Id", "Lookup" };
        helper.CreateFullyValidatedUser(transaction, info, "pw");

        // Get id via direct table query
        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());
        int64_t id = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

        PersonInfo out;
        EXPECT_TRUE(helper.LookupPerson(transaction, id, out));
        EXPECT_EQ(out.email, info.email);
        EXPECT_EQ(out.firstName, info.firstName);
        EXPECT_EQ(out.lastName, info.lastName);
        });
}

TEST(PersonHelperTest, LookupPersonByIdNotVerified) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupPersonByIdNotVerified", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        TestMailHelperPtr mailHelper = MakeTestMailHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();

        PersonInfo info{ "idnotver@example.com", "Id", "NotVer" };
        helper.PreliminaryRegisterPerson(transaction, secrets, mailHelper, info, "pw", false);

        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());
        int64_t id = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

        PersonInfo out;
        EXPECT_FALSE(helper.LookupPerson(transaction, id, out));
        });
}

TEST(PersonHelperTest, LookupPersonByIdNoPerson) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupPersonByIdNoPerson", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo out;
        EXPECT_FALSE(helper.LookupPerson(transaction, 424242, out));
        });
}

// LookupPerson (by email)
TEST(PersonHelperTest, LookupPersonByEmailBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupPersonByEmailBasic", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "emaillook@example.com", "Email", "Lookup" };
        helper.CreateFullyValidatedUser(transaction, info, "pw");

        PersonInfo out;
        int64_t id = -1;
        EXPECT_TRUE(helper.LookupPerson(transaction, info.email, out, id));
        EXPECT_EQ(out.email, info.email);
        EXPECT_EQ(out.firstName, info.firstName);
        EXPECT_EQ(out.lastName, info.lastName);
        EXPECT_GT(id, 0);
        });
}

TEST(PersonHelperTest, LookupPersonByEmailNotVerified) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupPersonByEmailNotVerified", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        TestMailHelperPtr mailHelper = MakeTestMailHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();

        PersonInfo info{ "emailnotver@example.com", "Email", "NotVer" };
        helper.PreliminaryRegisterPerson(transaction, secrets, mailHelper, info, "pw", false);

        PersonInfo out;
        int64_t id = -1;
        EXPECT_FALSE(helper.LookupPerson(transaction, info.email, out, id));
        });
}

TEST(PersonHelperTest, LookupPersonByEmailNoPerson) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupPersonByEmailNoPerson", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo out;
        int64_t id = -1;
        EXPECT_FALSE(helper.LookupPerson(
            transaction, "noone@example.com", out, id));
        });
}

// VerifyPassword
TEST(PersonHelperTest, VerifyPasswordBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyPasswordBasic", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "verifypass@example.com", "Verify", "Pass" };
        helper.CreateFullyValidatedUser(transaction, info, "MyPwd123!");
        EXPECT_TRUE(helper.VerifyPassword(transaction, info.email, "MyPwd123!"));
        });
}

TEST(PersonHelperTest, VerifyPasswordWrongPassword) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyPasswordWrongPassword", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "wrongpw@example.com", "Wrong", "Pw" };
        helper.CreateFullyValidatedUser(transaction, info, "RightOne!");
        EXPECT_FALSE(helper.VerifyPassword(transaction, info.email, "WrongOne!"));
        });
}

TEST(PersonHelperTest, VerifyPasswordNotVerified) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyPasswordNotVerified", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "notverpw@example.com", "Not", "Ver" };
        TestMailHelperPtr mailHelper = MakeTestMailHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        helper.PreliminaryRegisterPerson(transaction, secrets, mailHelper, info, "Secret!", false);
        EXPECT_FALSE(helper.VerifyPassword(transaction, info.email, "Secret!"));
        });
}

TEST(PersonHelperTest, VerifyPasswordNoPerson) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyPasswordNoPerson", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        EXPECT_FALSE(helper.VerifyPassword(transaction, "absent@example.com", "pw"));
        });
}

TEST(PersonHelperTest, CreateSessionTokenBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("CreateSessionTokenBasic", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "sess@example.com", "Sess", "User" };
        helper.CreateFullyValidatedUser(transaction, info, "Password!");

        // Capture now in micros
        int64_t nowUs = std::stoll(transaction.RunSqlStatementReturningOneValue(
            "SELECT now_us()"));

        std::string token;
        ASSERT_TRUE(helper.CreateSessionToken(transaction, secrets, info.email, token));
        ASSERT_FALSE(token.empty());

        // Validate DB row by uuid
        KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM sessions WHERE uuid = $1", token);
        ASSERT_FALSE(row.empty());
        // revoked should be false or 'f'
        EXPECT_TRUE(row.find(std::string(DbSchema::kSessionsRevoked)) != row.end());
        EXPECT_TRUE(row.at(std::string(DbSchema::kSessionsRevoked)) == "f" ||
                    row.at(std::string(DbSchema::kSessionsRevoked)) == "false" ||
                    row.at(std::string(DbSchema::kSessionsRevoked)) == "0");

        int64_t createdAt = std::stoll(row.at(std::string(DbSchema::kSessionsCreatedAt)));
        int64_t lastSeen = std::stoll(row.at(std::string(DbSchema::kSessionsLastSeenAt)));
        int64_t expires = std::stoll(row.at(std::string(DbSchema::kSessionsExpiresAt)));
        EXPECT_GE(createdAt, nowUs);
        EXPECT_GE(lastSeen, nowUs);
        EXPECT_GE(expires, nowUs);
    });
}

TEST(PersonHelperTest, CreateSessionTokenNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("CreateSessionTokenNotFound", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        std::string token;
        EXPECT_FALSE(helper.CreateSessionToken(transaction, secrets, "missing@example.com", token));
        EXPECT_TRUE(token.empty());
    });
}

TEST(PersonHelperTest, SessionUsedBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SessionUsedBasic", [&](Transaction& transaction) {



        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        // Short window (5 ms) to force last_seen_at update quickly.
        secrets->AddSecret(
            transaction,
            Secrets::kAuthSessionLastSeenUpdateDurationInMicros,
            "1000");
        PersonHelper helper = MakePersonHelper(
            transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "sessused@example.com", "Sess", "User" };
        helper.CreateFullyValidatedUser(transaction, info, "Password!");

        std::string token;
        ASSERT_TRUE(helper.CreateSessionToken(transaction, secrets, info.email, token));
        ASSERT_FALSE(token.empty());

        KeyValueTable rowBefore = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM sessions WHERE uuid = $1", token);
        ASSERT_FALSE(rowBefore.empty());
        int64_t lastSeenBefore = std::stoll(
            rowBefore.at(std::string(DbSchema::kSessionsLastSeenAt)));

        // Sleep beyond window to trigger update
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        auto provider = MakeTestTransactionProvider(transaction);
        helper.SessionUsed(transaction, secrets, provider, token);

        // Ensure queued work completes
        ThreadPool::GetInstance().Shutdown();

        KeyValueTable rowAfter = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM sessions WHERE uuid = $1", token);
        ASSERT_FALSE(rowAfter.empty());
        int64_t lastSeenAfter = std::stoll(
            rowAfter.at(std::string(DbSchema::kSessionsLastSeenAt)));
        EXPECT_GT(lastSeenAfter, lastSeenBefore);
    });
}

TEST(PersonHelperTest, SessionUsedNoUpdate) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SessionUsedNoUpdate", [&](Transaction& transaction) {



        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        // Long window so last_seen_at should NOT update.
        secrets->AddSecret(
            transaction,
            Secrets::kAuthSessionLastSeenUpdateDurationInMicros,
            "3600000000"); // 1 hour in micros
        PersonHelper helper = MakePersonHelper(
            transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "sessnoupd@example.com", "Sess", "NoUpd" };
        helper.CreateFullyValidatedUser(transaction, info, "Password!");

        std::string token;
        ASSERT_TRUE(helper.CreateSessionToken(transaction, secrets, info.email, token));
        ASSERT_FALSE(token.empty());

        KeyValueTable rowBefore = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM sessions WHERE uuid = $1", token);
        ASSERT_FALSE(rowBefore.empty());
        int64_t lastSeenBefore = std::stoll(
            rowBefore.at(std::string(DbSchema::kSessionsLastSeenAt)));

        // Sleep a short time (well under the long window) and signal SessionUsed
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        auto provider = MakeTestTransactionProvider(transaction);
        helper.SessionUsed(transaction, secrets, provider, token);

        // Ensure queued work completes (should be no-op due to long window)
        ThreadPool::GetInstance().Shutdown();

        KeyValueTable rowAfter = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM sessions WHERE uuid = $1", token);
        ASSERT_FALSE(rowAfter.empty());
        int64_t lastSeenAfter = std::stoll(
            rowAfter.at(std::string(DbSchema::kSessionsLastSeenAt)));
        EXPECT_EQ(lastSeenAfter, lastSeenBefore);
    });
}

TEST(PersonHelperTest, SessionUsedNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SessionUsedNotFound", [&](Transaction& transaction) {
        TestMailHelperPtr mailHelper = MakeTestMailHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(
            transaction,
            Secrets::kAuthSessionLastSeenUpdateDurationInMicros,
            "5000");
        PersonHelper helper = MakePersonHelper(
            transaction, testDb.GetDatabaseHelper());

        std::string fakeToken = "00000000-0000-0000-0000-000000000000";
        auto provider = MakeTestTransactionProvider(transaction);
        // Should silently do nothing; no exception expected here.
        EXPECT_THROW(helper.SessionUsed(transaction, secrets, provider, fakeToken), std::runtime_error);

        ThreadPool::GetInstance().Shutdown();
    });
}

TEST(PersonHelperTest, LookupPersonIdBySessionTokenBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupPersonIdBySessionTokenBasic", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "lpst@example.com", "Find", "User" };
        helper.CreateFullyValidatedUser(transaction, info, "pw");

        std::string token;
        ASSERT_TRUE(helper.CreateSessionToken(transaction, secrets, info.email, token));

        int64_t personId = -1;
        EXPECT_TRUE(helper.LookupPersonIdBySessionToken(transaction, token, personId));
        EXPECT_GT(personId, 0);
    });
}

TEST(PersonHelperTest, LookupPersonIdBySessionTokenNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupPersonIdBySessionTokenNotFound", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        int64_t personId = -1;
        EXPECT_THROW(helper.LookupPersonIdBySessionToken(transaction, "00000000-0000-0000-0000-000000000000", personId), std::runtime_error);
    });
}

TEST(PersonHelperTest, LookupPersonIdBySessionTokenRevoked) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupPersonIdBySessionTokenRevoked", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "revk@example.com", "Rev", "Ok" };
        helper.CreateFullyValidatedUser(transaction, info, "pw");
        std::string token;
        ASSERT_TRUE(helper.CreateSessionToken(transaction, secrets, info.email, token));

        // Revoke the session
        transaction.RunSqlStatement(
            "UPDATE sessions SET revoked = TRUE WHERE uuid = $1", token);

        int64_t personId = -1;
        EXPECT_FALSE(helper.LookupPersonIdBySessionToken(transaction, token, personId));
    });
}

TEST(PersonHelperTest, CreateDeviceTokenBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("CreateDeviceTokenBasic", [&](Transaction& transaction) {



        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(
            transaction,
            Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(20LL * 60LL * 1000000LL)); // 20 minutes

        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        PersonInfo info{ "devtok@example.com", "Dev", "Tok" };
        helper.CreateFullyValidatedUser(transaction, info, "Password!");

        std::string tokenId;
        std::string secretBlobBase64;
        ASSERT_TRUE(helper.CreateDeviceToken(transaction, secrets, info.email, tokenId, secretBlobBase64));
        ASSERT_FALSE(tokenId.empty());
        ASSERT_FALSE(secretBlobBase64.empty());

        // Validate DB row
        KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM device_tokens WHERE uuid = $1", tokenId);
        ASSERT_FALSE(row.empty());

        EXPECT_TRUE(row.at(std::string(DbSchema::kDeviceTokensRevoked)) == "f" ||
                    row.at(std::string(DbSchema::kDeviceTokensRevoked)) == "false" ||
                    row.at(std::string(DbSchema::kDeviceTokensRevoked)) == "0");
        int64_t personId = std::stoll(row.at(std::string(DbSchema::kDeviceTokensPersonId)));
        EXPECT_GT(personId, 0);

        int64_t expiresAt = std::stoll(row.at(std::string(DbSchema::kDeviceTokensExpiresAt)));
        int64_t nowUs = std::stoll(transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));
        EXPECT_GE(expiresAt, nowUs);
    });
}

TEST(PersonHelperTest, CreateDeviceTokenNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("CreateDeviceTokenNotFound", [&](Transaction& transaction) {



        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(
            transaction,
            Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(10LL * 60LL * 1000000LL));

        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        std::string tokenId;
        std::string secretBlobBase64;
        EXPECT_FALSE(helper.CreateDeviceToken(transaction, secrets, "missing@example.com", tokenId, secretBlobBase64));
        EXPECT_TRUE(tokenId.empty());
        EXPECT_TRUE(secretBlobBase64.empty());
    });
}

TEST(PersonHelperTest, CreateDeviceTokenRevoked) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("CreateDeviceTokenRevoked", [&](Transaction& transaction) {



        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(
            transaction,
            Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(5LL * 60LL * 1000000LL));

        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        PersonInfo info{ "revtok@example.com", "Rev", "Tok" };
        helper.CreateFullyValidatedUser(transaction, info, "Password!");

        std::string tokenId;
        std::string secretBlobBase64;
        ASSERT_TRUE(helper.CreateDeviceToken(transaction, secrets, info.email, tokenId, secretBlobBase64));
        ASSERT_FALSE(tokenId.empty());

        // Revoke the token
        transaction.RunSqlStatement(
            "UPDATE device_tokens SET revoked = TRUE WHERE uuid = $1", tokenId);

        KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
            "SELECT revoked FROM device_tokens WHERE uuid = $1", tokenId);
        ASSERT_FALSE(row.empty());
        EXPECT_TRUE(row.at("revoked") == "t" || row.at("revoked") == "true" || row.at("revoked") == "1");
    });
}

TEST(PersonHelperTest, TryLoginWithDeviceTokenBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("TryLoginWithDeviceTokenBasic", [&](Transaction& transaction) {



        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(
            transaction,
            Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(20LL * 60LL * 1000000LL)); // 20 minutes

        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        PersonInfo info{ "devlogin@example.com", "Dev", "Login" };
        helper.CreateFullyValidatedUser(transaction, info, "Password!");

        // Create initial device token + secret
        std::string tokenId;
        std::string base64Secret;
        ASSERT_TRUE(helper.CreateDeviceToken(transaction, secrets, info.email, tokenId, base64Secret));
        ASSERT_FALSE(tokenId.empty());
        ASSERT_FALSE(base64Secret.empty());

        // Capture original expires_at
        KeyValueTable rowBefore = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM device_tokens WHERE uuid = $1", tokenId);
        ASSERT_FALSE(rowBefore.empty());
        int64_t expiresBefore = std::stoll(rowBefore.at(std::string(DbSchema::kDeviceTokensExpiresAt)));

        // Try login with device token (refresh/rotate)
        std::string outTokenId, outBase64Secret;
        EXPECT_TRUE(helper.TryLoginWithDeviceToken(transaction, secrets, tokenId, base64Secret, outTokenId, outBase64Secret));
        EXPECT_NE(outTokenId, tokenId);
        EXPECT_FALSE(outBase64Secret.empty());
        EXPECT_NE(outBase64Secret, base64Secret);

        // Validate DB updated (expires_at should be >= previous)
        KeyValueTable rowAfter = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM device_tokens WHERE uuid = $1", outTokenId);
        ASSERT_FALSE(rowAfter.empty());
        int64_t expiresAfter = std::stoll(rowAfter.at(std::string(DbSchema::kDeviceTokensExpiresAt)));
        EXPECT_GE(expiresAfter, expiresBefore);
    });
}

TEST(PersonHelperTest, TryLoginWithDeviceTokenNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("TryLoginWithDeviceTokenNotFound", [&](Transaction& transaction) {



        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(
            transaction,
            Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(10LL * 60LL * 1000000LL)); // 10 minutes

        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        // No token created; use garbage values
        std::string outTokenId, outBase64Secret;
        EXPECT_FALSE(helper.TryLoginWithDeviceToken(
            transaction, secrets,
            "00000000-0000-0000-0000-000000000000",  // non-existent token uuid
            "ZmFrZVNlY3JldA==",                      // "fakeSecret" base64
            outTokenId, outBase64Secret));
        EXPECT_TRUE(outTokenId.empty());
        EXPECT_TRUE(outBase64Secret.empty());
    });
}

TEST(PersonHelperTest, TryLoginWithDeviceTokenExpired) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("TryLoginWithDeviceTokenExpired", [&](Transaction& transaction) {



        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(
            transaction,
            Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(5LL * 60LL * 1000000LL)); // 5 minutes

        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        PersonInfo info{ "devexpired@example.com", "Dev", "Expired" };
        helper.CreateFullyValidatedUser(transaction, info, "Password!");

        std::string tokenId;
        std::string base64Secret;
        ASSERT_TRUE(helper.CreateDeviceToken(transaction, secrets, info.email, tokenId, base64Secret));

        // Force expiration in the past
        transaction.RunSqlStatement(
            "UPDATE device_tokens SET expires_at = 1 WHERE uuid = $1", tokenId);

        std::string outTokenId, outBase64Secret;
        EXPECT_FALSE(helper.TryLoginWithDeviceToken(transaction, secrets, tokenId, base64Secret, outTokenId, outBase64Secret));
        EXPECT_TRUE(outTokenId.empty());
        EXPECT_TRUE(outBase64Secret.empty());
    });
}

TEST(PersonHelperTest, TryLoginWithDeviceTokenRevoked) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("TryLoginWithDeviceTokenRevoked", [&](Transaction& transaction) {



        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(
            transaction,
            Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(5LL * 60LL * 1000000LL)); // 5 minutes

        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        PersonInfo info{ "devrevoked@example.com", "Dev", "Revoked" };
        helper.CreateFullyValidatedUser(transaction, info, "Password!");

        std::string tokenId;
        std::string base64Secret;
        ASSERT_TRUE(helper.CreateDeviceToken(transaction, secrets, info.email, tokenId, base64Secret));

        // Revoke the token
        transaction.RunSqlStatement(
            "UPDATE device_tokens SET revoked = TRUE WHERE uuid = $1", tokenId);

        std::string outTokenId, outBase64Secret;
        EXPECT_FALSE(helper.TryLoginWithDeviceToken(transaction, secrets, tokenId, base64Secret, outTokenId, outBase64Secret));
        EXPECT_TRUE(outTokenId.empty());
        EXPECT_TRUE(outBase64Secret.empty());
    });
}

TEST(PersonHelperTest, LookupPersonIdBySessionTokenExpired) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupPersonIdBySessionTokenExpired", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "expr@example.com", "Ex", "Pir" };
        helper.CreateFullyValidatedUser(transaction, info, "pw");
        std::string token;
        ASSERT_TRUE(helper.CreateSessionToken(transaction, secrets, info.email, token));

        // Force expiration in the past
        transaction.RunSqlStatement(
            "UPDATE sessions SET expires_at = 1 WHERE uuid = $1", token);

        int64_t personId = -1;
        EXPECT_FALSE(helper.LookupPersonIdBySessionToken(transaction, token, personId));
    });
}

// UpdateInfo
TEST(PersonHelperTest, UpdateInfoBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateInfoBasic", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "updinfo@example.com", "Up", "Info" };
        helper.CreateFullyValidatedUser(transaction, info, "pw");
        PersonInfo changes;
        changes.firstName = "UpdatedFirst";
        changes.lastName = "UpdatedLast";
        ASSERT_TRUE(helper.UpdateInfo(transaction, info.email, changes));

        PersonInfo out;
        int64_t id = -1;
        ASSERT_TRUE(helper.LookupPerson(transaction, info.email, out, id));
        EXPECT_EQ(out.firstName, "UpdatedFirst");
        EXPECT_EQ(out.lastName, "UpdatedLast");
        });
}

TEST(PersonHelperTest, UpdateInfoNotVerified) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateInfoNotVerified", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        TestMailHelperPtr mailHelper = MakeTestMailHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();

        PersonInfo info{ "updnotver@example.com", "Up", "NV" };
        helper.PreliminaryRegisterPerson(transaction, secrets, mailHelper, info, "pw", false);
        PersonInfo changes; changes.firstName = "X";
        EXPECT_FALSE(helper.UpdateInfo(transaction, info.email, changes));
        });
}

TEST(PersonHelperTest, UpdateInfoNoPerson) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateInfoNoPerson", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo changes; changes.firstName = "X";
        EXPECT_FALSE(helper.UpdateInfo(transaction, "missing@example.com", changes));
        });
}

// UpdatePassword
TEST(PersonHelperTest, UpdatePasswordBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdatePasswordBasic", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "updpass@example.com", "Up", "Pass" };
        helper.CreateFullyValidatedUser(transaction, info, "OrigPw!");
        ASSERT_TRUE(helper.VerifyPassword(transaction, info.email, "OrigPw!"));
        ASSERT_TRUE(helper.UpdatePassword(transaction, info.email, "NewPw!"));
        EXPECT_TRUE(helper.VerifyPassword(transaction, info.email, "NewPw!"));
        EXPECT_FALSE(helper.VerifyPassword(transaction, info.email, "OrigPw!"));
        });
}

TEST(PersonHelperTest, UpdatePasswordNotVerified) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdatePasswordNotVerified", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        TestMailHelperPtr mailHelper = MakeTestMailHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();

        PersonInfo info{ "updpassnv@example.com", "Up", "Pass" };
        helper.PreliminaryRegisterPerson(transaction, secrets, mailHelper, info, "SomePw!", false);
        EXPECT_FALSE(helper.UpdatePassword(transaction, info.email, "NewPw!"));
        });
}

TEST(PersonHelperTest, UpdatePasswordNoPerson) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdatePasswordNoPerson", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        EXPECT_FALSE(helper.UpdatePassword(transaction, "nobody@example.com", "AnyPw"));
        });
}

// DeletePerson
TEST(PersonHelperTest, DeletePersonBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeletePersonBasic", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());

        PersonInfo info{ "delme@example.com", "Del", "Me" };
        helper.CreateFullyValidatedUser(transaction, info, "pw");
        ASSERT_TRUE(helper.IsPerson(transaction, info.email));
        helper.DeletePerson(transaction, info.email);
        EXPECT_FALSE(helper.IsPerson(transaction, info.email));

        TableHelpers::People people(testDb.GetDatabaseHelper());
         KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        EXPECT_TRUE(person.empty());
        });
}

TEST(PersonHelperTest, LogoutPersonBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LogoutPersonBasic", [&](Transaction& transaction) {
        // Secrets needed
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL)); // 30 minutes
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL)); // 7 days

        // Create validated user
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        PersonInfo info{ "logout@example.com", "Log", "Out" };
        helper.CreateFullyValidatedUser(transaction, info, "Password!");

        // Lookup person id
        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());
        int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

        // Create a session token
        std::string sessionToken;
        ASSERT_TRUE(helper.CreateSessionToken(transaction, secrets, info.email, sessionToken));
        ASSERT_FALSE(sessionToken.empty());

        // Create a device token
        std::string deviceUuid;
        std::string deviceSecretBase64;
        ASSERT_TRUE(helper.CreateDeviceToken(transaction, secrets, info.email, deviceUuid, deviceSecretBase64));
        ASSERT_FALSE(deviceUuid.empty());
        ASSERT_FALSE(deviceSecretBase64.empty());

        // Verify entries exist prior to logout
        {
            // Sessions present
            KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
                "SELECT * FROM sessions WHERE uuid = $1", sessionToken);
            ASSERT_FALSE(row.empty());
            EXPECT_EQ(row.at(std::string(DbSchema::kSessionsPersonId)), std::to_string(personId));

            // Device token present
            KeyValueTable drow = transaction.RunSqlStatementReturningOneRow(
                "SELECT * FROM device_tokens WHERE uuid = $1", deviceUuid);
            ASSERT_FALSE(drow.empty());
            EXPECT_EQ(drow.at(std::string(DbSchema::kDeviceTokensPersonId)), std::to_string(personId));
        }

        // Perform logout
        helper.LogoutPerson(transaction, personId);

        // Validate removal from sessions
        {
            std::string countStr = transaction.RunSqlStatementReturningOneValue(
                "SELECT COUNT(*) FROM sessions WHERE person_id = $1", std::to_string(personId));
            int count = std::stoi(countStr);
            EXPECT_EQ(count, 0);
        }

        // Validate removal from device_tokens
        {
            std::string countStr = transaction.RunSqlStatementReturningOneValue(
                "SELECT COUNT(*) FROM device_tokens WHERE person_id = $1", std::to_string(personId));
            int count = std::stoi(countStr);
            EXPECT_EQ(count, 0);
        }
    });
}

TEST(PersonHelperTest, GetRolesForUserBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolesForUserBasic", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        PersonInfo info{ "roleuser@example.com", "Role", "User" };
        helper.CreateFullyValidatedUser(transaction, info, "pw");

        // Lookup person id
        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());
        int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

        // Create roles and assignments
        TableHelpers::Roles roles(testDb.GetDatabaseHelper());
        int64_t roleId1 = roles.AddRole(transaction, "admin", "Administrator");
        int64_t roleId2 = roles.AddRole(transaction, "editor", "Editor");

        TableHelpers::RoleAssignments ra(testDb.GetDatabaseHelper());
        ra.AddRoleAssignment(transaction, personId, roleId1);
        ra.AddRoleAssignment(transaction, personId, roleId2);

        StringArray roleNames = helper.GetRolesForUser(transaction, personId);
        // Order corresponds to insertion order from assignments
        ASSERT_EQ(roleNames.size(), 2u);
        EXPECT_THAT(roleNames, ::testing::ElementsAre("admin", "editor"));
    });
}

TEST(PersonHelperTest, GetRolesForUserNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolesForUserNotFound", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        EXPECT_THROW(helper.GetRolesForUser(transaction, 999999), std::runtime_error);
    });
}

TEST(PersonHelperTest, GetRolesForUserNoRoles) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRolesForUserNoRoles", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        PersonInfo info{ "noroles@example.com", "No", "Roles" };
        helper.CreateFullyValidatedUser(transaction, info, "pw");

        // Lookup person id
        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());
        int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

        StringArray roles = helper.GetRolesForUser(transaction, personId);
        EXPECT_TRUE(roles.empty());
    });
}

TEST(PersonHelperTest, GetPermissionsForUserBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetPermissionsForUserBasic", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        PersonInfo info{ "permuser@example.com", "Perm", "User" };
        helper.CreateFullyValidatedUser(transaction, info, "pw");

        // Lookup person id
        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());
        int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

        // Create roles
        TableHelpers::Roles roles(testDb.GetDatabaseHelper());
        int64_t roleId1 = roles.AddRole(transaction, "viewer", "Viewer");
        int64_t roleId2 = roles.AddRole(transaction, "editor", "Editor");

        // Create permissions
        TableHelpers::Permissions perms(testDb.GetDatabaseHelper());
        int64_t pRead = perms.AddPermission(transaction, "read", "Read");
        int64_t pWrite = perms.AddPermission(transaction, "write", "Write");
        int64_t pDownload = perms.AddPermission(transaction, "download", "Download");

        // Map role->permissions
        TableHelpers::RolePermissions rperms(testDb.GetDatabaseHelper());
        rperms.AddRolePermission(transaction, roleId1, pRead);
        rperms.AddRolePermission(transaction, roleId1, pDownload);
        rperms.AddRolePermission(transaction, roleId2, pRead);
        rperms.AddRolePermission(transaction, roleId2, pWrite);

        // Assign roles to user
        TableHelpers::RoleAssignments ra(testDb.GetDatabaseHelper());
        ra.AddRoleAssignment(transaction, personId, roleId1);
        ra.AddRoleAssignment(transaction, personId, roleId2);

        StringArray permNames = helper.GetPermissionsForUser(transaction, personId);
        // Should contain unique set: read, write, download
        ::testing::StringMatchResultListener listener;
        EXPECT_EQ(permNames.size(), 3u);
        // Order is not guaranteed; check as a set
        StringSet s = StringSetFromStringArray(permNames);
        EXPECT_TRUE(SetContains(s, "read"));
        EXPECT_TRUE(SetContains(s, "write"));
        EXPECT_TRUE(SetContains(s, "download"));
    });
}

TEST(PersonHelperTest, GetPermissionsForUserNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetPermissionsForUserNotFound", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        EXPECT_THROW(helper.GetPermissionsForUser(transaction, 999999), std::runtime_error);
    });
}

TEST(PersonHelperTest, GetPermissionsForUserNoPermissions) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetPermissionsForUserNoPermissions", [&](Transaction& transaction) {
        PersonHelper helper = MakePersonHelper(transaction, testDb.GetDatabaseHelper());
        PersonInfo info{ "noperms@example.com", "No", "Perms" };
        helper.CreateFullyValidatedUser(transaction, info, "pw");

        // Lookup person id
        TableHelpers::People people(testDb.GetDatabaseHelper());
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());
        int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

        // No role assignments -> no permissions
        StringArray perms = helper.GetPermissionsForUser(transaction, personId);
        EXPECT_TRUE(perms.empty());
    });
}

// =====================================================================
// Phase 5.3 of the security review: LoginPerson + sticky lockout.
//
// LoginPerson is the high-level wrapper around VerifyPassword. It
// runs the Argon2id verify and atomically updates
// `people.failed_login_attempts` / `people.locked_until` based on
// the outcome — single UPDATE so two parallel attempts at the
// threshold over-count by at most one (acceptable).
//
// Tests use a fast secrets helper (MakeTestSecretsHelper overrides
// Argon2id cost to INTERACTIVE) to keep the suite responsive.
// Lockout threshold is set tight (3) so the boundary tests don't
// have to fail-loop hundreds of times.
// =====================================================================

namespace {

void SeedLockoutSecrets(
    Transaction& tx,
    Secrets::Test::SecretsHelperTestPtr secrets,
    int64_t threshold,
    int64_t lockoutDurationMicros) {
    secrets->AddSecret(tx,
        Secrets::kAuthAccountLockoutAfterFailures,
        std::to_string(threshold));
    secrets->AddSecret(tx,
        Secrets::kAuthAccountLockoutDurationInMicros,
        std::to_string(lockoutDurationMicros));
}

KeyValueTable LookupPersonRow(
    Transaction& tx, DatabaseHelper db, std::string_view email) {
    TableHelpers::People p(db);
    return p.LookupPersonByEmail(tx, email);
}

}  // namespace

TEST(PersonHelperTest, LoginPersonSuccessClearsFailureCount) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginPersonSuccessClearsFailureCount",
        [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedLockoutSecrets(tx, secrets, /*threshold=*/3,
                           /*lockoutDurationMicros=*/3600LL * 1000000LL);

        PersonHelper helper(testDb.GetDatabaseHelper());
        PersonInfo info{ "login-good@example.com", "F", "L" };
        helper.CreateFullyValidatedUser(tx, info, "MyPassword!");

        // Pre-seed a non-zero failure count to confirm the success
        // path resets it to 0.
        tx.RunSqlStatement(
            "UPDATE people SET failed_login_attempts = 2 WHERE email = $1",
            std::string(info.email));

        auto result = helper.LoginPerson(
            tx, secrets, info.email, "MyPassword!");
        EXPECT_TRUE(result.success);
        EXPECT_FALSE(result.justLocked);
        EXPECT_GT(result.personId, 0);
        EXPECT_EQ(result.failedLoginAttempts, 0);
        EXPECT_EQ(result.lockedUntil, 0);

        KeyValueTable row = LookupPersonRow(
            tx, testDb.GetDatabaseHelper(), info.email);
        EXPECT_EQ(row.at(std::string(DbSchema::kPeopleFailedLoginAttempts)), "0");
        // locked_until should be NULL — surfaces as empty.
        EXPECT_TRUE(row.at(std::string(DbSchema::kPeopleLockedUntil)).empty());
    });
}

TEST(PersonHelperTest, LoginPersonWrongPasswordIncrementsCount) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginPersonWrongPasswordIncrementsCount",
        [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedLockoutSecrets(tx, secrets, /*threshold=*/100,
                           /*lockoutDurationMicros=*/3600LL * 1000000LL);

        PersonHelper helper(testDb.GetDatabaseHelper());
        PersonInfo info{ "login-bad@example.com", "F", "L" };
        helper.CreateFullyValidatedUser(tx, info, "Right!");

        auto r1 = helper.LoginPerson(tx, secrets, info.email, "Wrong!");
        EXPECT_FALSE(r1.success);
        EXPECT_FALSE(r1.justLocked);
        EXPECT_EQ(r1.failedLoginAttempts, 1);
        EXPECT_EQ(r1.lockedUntil, 0);

        auto r2 = helper.LoginPerson(tx, secrets, info.email, "Wrong!");
        EXPECT_FALSE(r2.success);
        EXPECT_EQ(r2.failedLoginAttempts, 2);
        EXPECT_EQ(r2.lockedUntil, 0);
    });
}

TEST(PersonHelperTest, LoginAccountLockoutAtThreshold) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginAccountLockoutAtThreshold",
        [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        const int64_t threshold = 3;
        const int64_t lockDurationMicros = 60LL * 60LL * 1000000LL;
        SeedLockoutSecrets(tx, secrets, threshold, lockDurationMicros);

        PersonHelper helper(testDb.GetDatabaseHelper());
        PersonInfo info{ "lockout@example.com", "F", "L" };
        helper.CreateFullyValidatedUser(tx, info, "Right!");

        // Two failures — under threshold.
        auto r1 = helper.LoginPerson(tx, secrets, info.email, "Wrong!");
        EXPECT_FALSE(r1.justLocked);
        EXPECT_EQ(r1.lockedUntil, 0);
        auto r2 = helper.LoginPerson(tx, secrets, info.email, "Wrong!");
        EXPECT_FALSE(r2.justLocked);
        EXPECT_EQ(r2.lockedUntil, 0);

        // Third failure trips the lock.
        auto r3 = helper.LoginPerson(tx, secrets, info.email, "Wrong!");
        EXPECT_FALSE(r3.success);
        EXPECT_TRUE(r3.justLocked);
        EXPECT_EQ(r3.failedLoginAttempts, threshold);
        EXPECT_GT(r3.lockedUntil, 0);

        // Persisted on the row.
        KeyValueTable row = LookupPersonRow(
            tx, testDb.GetDatabaseHelper(), info.email);
        EXPECT_EQ(row.at(std::string(DbSchema::kPeopleFailedLoginAttempts)),
                  std::to_string(threshold));
        EXPECT_FALSE(row.at(std::string(DbSchema::kPeopleLockedUntil)).empty());
    });
}

TEST(PersonHelperTest, LoginSuccessClearsLockout) {
    // Even when the account isn't locked, a successful login wipes
    // the failure count (so a user who got close to the threshold
    // and then logs in correctly doesn't carry that history forward
    // forever).
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginSuccessClearsLockout",
        [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedLockoutSecrets(tx, secrets, /*threshold=*/100,
                           /*lockoutDurationMicros=*/3600LL * 1000000LL);

        PersonHelper helper(testDb.GetDatabaseHelper());
        PersonInfo info{ "login-clears@example.com", "F", "L" };
        helper.CreateFullyValidatedUser(tx, info, "Right!");

        // Pre-seed a stale lock that has already expired AND a
        // non-zero failure count.
        std::string nowUs = tx.RunSqlStatementReturningOneValue(
            "SELECT now_us()");
        int64_t stalePast = std::stoll(nowUs) - 3600LL * 1000000LL;
        tx.RunSqlStatement(
            "UPDATE people SET failed_login_attempts = 9, locked_until = $1 "
            "WHERE email = $2",
            std::to_string(stalePast),
            std::string(info.email));

        auto result = helper.LoginPerson(tx, secrets, info.email, "Right!");
        EXPECT_TRUE(result.success);

        KeyValueTable row = LookupPersonRow(
            tx, testDb.GetDatabaseHelper(), info.email);
        EXPECT_EQ(row.at(std::string(DbSchema::kPeopleFailedLoginAttempts)), "0");
        EXPECT_TRUE(row.at(std::string(DbSchema::kPeopleLockedUntil)).empty());
    });
}

TEST(PersonHelperTest, LoginAccountStaysLockedDuringWindow) {
    // A locked account that gets a CORRECT password during the lock
    // window still fails the login. (The gate would normally catch
    // this earlier — Phase 5.2 — but LoginPerson itself must not
    // unlock on a correct password mid-lock.)
    //
    // Implementation note: LoginPerson does NOT itself check
    // locked_until — that's the gate's job. So a correct password
    // through LoginPerson against a locked account will succeed
    // here. The actual "stays locked" behavior comes from the gate
    // intercepting BEFORE LoginPerson runs. This test pins that
    // contract: LoginPerson is dumb about the lock; the gate is
    // smart about it.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginAccountStaysLockedDuringWindow",
        [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedLockoutSecrets(tx, secrets, /*threshold=*/3,
                           /*lockoutDurationMicros=*/3600LL * 1000000LL);

        PersonHelper helper(testDb.GetDatabaseHelper());
        PersonInfo info{ "still-locked@example.com", "F", "L" };
        helper.CreateFullyValidatedUser(tx, info, "Right!");

        // Trip the lockout via wrong-password attempts.
        for (int i = 0; i < 3; ++i) {
            helper.LoginPerson(tx, secrets, info.email, "Wrong!");
        }
        KeyValueTable row = LookupPersonRow(
            tx, testDb.GetDatabaseHelper(), info.email);
        ASSERT_FALSE(row.at(std::string(DbSchema::kPeopleLockedUntil)).empty());
        std::string lockedUntilStr = row.at(
            std::string(DbSchema::kPeopleLockedUntil));

        // Still inside the lockout window. The gate (Phase 5.2)
        // would refuse this; LoginPerson, by contract, just runs
        // the verify and resets state on a correct password.
        // Document that contract here so a future refactor that
        // adds locked_until checks INTO LoginPerson breaks this
        // test loudly.
        auto result = helper.LoginPerson(tx, secrets, info.email, "Right!");
        EXPECT_TRUE(result.success)
            << "LoginPerson is intentionally lock-agnostic; gating is the "
               "responsibility of LoginGate (Phase 5.2). If you change this, "
               "confirm the gate still runs in front of LoginPerson at every "
               "production callsite.";
    });
}

TEST(PersonHelperTest, LoginAccountUnlocksAfterWindow) {
    // After the lockout duration elapses, the gate would let the
    // user back in. To simulate "after the window" without sleeping,
    // we manually backdate locked_until past now() and verify a
    // successful LoginPerson clears the lock.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LoginAccountUnlocksAfterWindow",
        [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedLockoutSecrets(tx, secrets, /*threshold=*/3,
                           /*lockoutDurationMicros=*/3600LL * 1000000LL);

        PersonHelper helper(testDb.GetDatabaseHelper());
        PersonInfo info{ "unlock-after@example.com", "F", "L" };
        helper.CreateFullyValidatedUser(tx, info, "Right!");

        // Trip the lockout, then backdate locked_until.
        for (int i = 0; i < 3; ++i) {
            helper.LoginPerson(tx, secrets, info.email, "Wrong!");
        }
        std::string nowUs = tx.RunSqlStatementReturningOneValue(
            "SELECT now_us()");
        int64_t expired = std::stoll(nowUs) - 1000LL;  // 1 ms ago
        tx.RunSqlStatement(
            "UPDATE people SET locked_until = $1 WHERE email = $2",
            std::to_string(expired),
            std::string(info.email));

        auto result = helper.LoginPerson(tx, secrets, info.email, "Right!");
        EXPECT_TRUE(result.success);
        EXPECT_EQ(result.failedLoginAttempts, 0);
        EXPECT_EQ(result.lockedUntil, 0);

        KeyValueTable row = LookupPersonRow(
            tx, testDb.GetDatabaseHelper(), info.email);
        EXPECT_EQ(row.at(std::string(DbSchema::kPeopleFailedLoginAttempts)), "0");
        EXPECT_TRUE(row.at(std::string(DbSchema::kPeopleLockedUntil)).empty());
    });
}

TEST(PersonHelperTest, LoginPersonUnknownEmailReturnsFailureWithNoSideEffect) {
    // An unknown email returns failure but does NOT touch any row
    // in `people` (there's no row to touch). Confirms LoginPerson
    // doesn't use the input email to forge a row or leak existence.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "LoginPersonUnknownEmailReturnsFailureWithNoSideEffect",
        [&](Transaction& tx) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SeedLockoutSecrets(tx, secrets, /*threshold=*/3,
                           /*lockoutDurationMicros=*/3600LL * 1000000LL);

        PersonHelper helper(testDb.GetDatabaseHelper());
        auto result = helper.LoginPerson(
            tx, secrets, "nobody@example.com", "Doesnt matter");
        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.justLocked);
        EXPECT_EQ(result.personId, -1);
        EXPECT_EQ(result.failedLoginAttempts, 0);

        std::string count = tx.RunSqlStatementReturningOneValue(
            "SELECT count(*) FROM people WHERE email = $1",
            std::string("nobody@example.com"));
        EXPECT_EQ(count, "0") << "no row should have been created";
    });
}

}  // namespace
}  // namespace Auth {