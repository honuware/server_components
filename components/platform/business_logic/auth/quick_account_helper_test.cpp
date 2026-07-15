#include "quick_account_helper.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "business_logic/auth/person.h"
#include "db_schema/people.h"
#include "sql_util/table_helpers/people.h"
#include "test/src/util/database_test_helper.h"
#include "util/mail/mail_helper_test_util.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"

namespace Auth {
namespace {

using ::Mail::Test::MakeTestMailHelper;
using ::Mail::Test::TestMailHelperPtr;
using ::testing::HasSubstr;

TEST(QuickAccountHelperTest, CreatesAccountWithPasswordAndWelcomeEmail) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("QuickAccountCreate", [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        TestMailHelperPtr mailHelper = MakeTestMailHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        // The welcome-email subject substitutes the studio name from branding
        // (kMailSenderName). Set it explicitly so this passes in a framework-only
        // build too (which registers no app brand default).
        secrets->AddSecretTest(Secrets::kMailSenderName, "Test Studio");
        QuickAccountHelper helper(db, secrets, mailHelper);

        QuickAccountResult result = helper.EnsureAccountWithWelcome(
            tx, "Maya", "Patel", "maya@example.com");
        EXPECT_GT(result.personId, 0);
        EXPECT_FALSE(result.alreadyExists);
        EXPECT_EQ(result.firstName, "Maya");
        EXPECT_EQ(result.lastName, "Patel");

        // The person exists, is flagged to change the temporary password,
        // and has a REAL password hash (not the empty hash that locked
        // walk-in people out before this helper existed).
        TableHelpers::People people(db);
        KeyValueTable person = people.GetPersonById(tx, result.personId);
        EXPECT_EQ(person.at(std::string(DbSchema::kPeopleEmail)),
                  "maya@example.com");
        {
            const std::string& mustChange =
                person.at(std::string(DbSchema::kPeopleMustChangePassword));
            EXPECT_TRUE(mustChange == "t" || mustChange == "true") << mustChange;
        }
        EXPECT_FALSE(person.at(std::string(DbSchema::kPeoplePasswordHash)).empty());

        // Exactly one welcome email, to the new person, carrying a password.
        ASSERT_EQ(mailHelper->GetMessages().size(), 1u);
        const auto& msg = mailHelper->GetMessages()[0];
        ASSERT_EQ(msg.GetTo().size(), 1u);
        EXPECT_EQ(msg.GetTo()[0].address, "maya@example.com");
        // Subject uses the studio name from branding (kMailSenderName), set above.
        EXPECT_EQ(msg.GetSubject(),
                  "Welcome to Test Studio - Your Account");
        EXPECT_THAT(msg.GetBodyHtml(), HasSubstr("Maya"));
    });
}

TEST(QuickAccountHelperTest, TemporaryPasswordInEmailActuallyLogsIn) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("QuickAccountPassword", [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        TestMailHelperPtr mailHelper = MakeTestMailHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        QuickAccountHelper helper(db, secrets, mailHelper);

        helper.EnsureAccountWithWelcome(tx, "Pat", "Lee", "pat@example.com");

        // Pull the temporary password out of the welcome email body and
        // verify it against the stored hash — the end-to-end "can this
        // person actually log in" guarantee. The template renders it as
        // <span class="password">XXXX</span>.
        ASSERT_EQ(mailHelper->GetMessages().size(), 1u);
        const std::string& body = mailHelper->GetMessages()[0].GetBodyHtml();
        const std::string marker = "<span class=\"password\">";
        auto start = body.find(marker);
        ASSERT_NE(start, std::string::npos);
        start += marker.size();
        auto end = body.find("</span>", start);
        ASSERT_NE(end, std::string::npos);
        std::string temporaryPassword = body.substr(start, end - start);
        ASSERT_FALSE(temporaryPassword.empty());

        PersonHelper personHelper(db);
        EXPECT_TRUE(personHelper.VerifyPassword(
            tx, "pat@example.com", temporaryPassword));
    });
}

TEST(QuickAccountHelperTest, ReusesExistingAccountWithoutEmail) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("QuickAccountReuse", [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        TestMailHelperPtr mailHelper = MakeTestMailHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();

        TableHelpers::People people(db);
        int64_t existingId = people.AddPerson(
            tx, "sam@example.com", "Sam", "Student", "existing-hash");

        QuickAccountHelper helper(db, secrets, mailHelper);
        QuickAccountResult result = helper.EnsureAccountWithWelcome(
            tx, "Different", "Name", "sam@example.com");

        // Existing account wins: same id, stored names, NO welcome email
        // (their password is whatever they already have).
        EXPECT_EQ(result.personId, existingId);
        EXPECT_TRUE(result.alreadyExists);
        EXPECT_EQ(result.firstName, "Sam");
        EXPECT_EQ(result.lastName, "Student");
        EXPECT_TRUE(mailHelper->GetMessages().empty());

        KeyValueTable person = people.GetPersonById(tx, existingId);
        EXPECT_EQ(person.at(std::string(DbSchema::kPeoplePasswordHash)),
                  "existing-hash");
    });
}

TEST(QuickAccountHelperTest, NullMailStillCreatesUsableAccount) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("QuickAccountNoMail", [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        QuickAccountHelper helper(db, nullptr, nullptr);

        QuickAccountResult result = helper.EnsureAccountWithWelcome(
            tx, "Quiet", "Person", "quiet@example.com");
        EXPECT_GT(result.personId, 0);
        EXPECT_FALSE(result.alreadyExists);

        // No email goes out, but the account is still real (hash +
        // must_change_password) — never an empty-hash orphan.
        TableHelpers::People people(db);
        KeyValueTable person = people.GetPersonById(tx, result.personId);
        EXPECT_FALSE(person.at(std::string(DbSchema::kPeoplePasswordHash)).empty());
        {
            const std::string& mustChange =
                person.at(std::string(DbSchema::kPeopleMustChangePassword));
            EXPECT_TRUE(mustChange == "t" || mustChange == "true") << mustChange;
        }
    });
}

// The post-creation hook fires exactly once for a newly created account,
// inside the same transaction, with the new person's id and email.
TEST(QuickAccountHelperTest, PostCreateHookInvokedWithPersonIdAndEmail) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("QuickAccountHookInvoked", [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();

        int hookCalls = 0;
        int64_t hookPersonId = 0;
        std::string hookEmail;
        QuickAccountPostCreateHook postCreate =
            [&](Transaction&, int64_t personId, const std::string& email) {
                ++hookCalls;
                hookPersonId = personId;
                hookEmail = email;
            };
        QuickAccountHelper helper(db, secrets, nullptr, postCreate);

        QuickAccountResult result = helper.EnsureAccountWithWelcome(
            tx, "Hooky", "User", "hooky@example.com");

        EXPECT_EQ(hookCalls, 1);
        EXPECT_EQ(hookPersonId, result.personId);
        EXPECT_EQ(hookEmail, "hooky@example.com");
    });
}

// The hook must NOT fire when an existing account is reused — matching the
// pre-refactor behavior where gift processing only ran on the create path.
TEST(QuickAccountHelperTest, PostCreateHookNotInvokedWhenAccountReused) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("QuickAccountHookReuse", [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();

        TableHelpers::People people(db);
        people.AddPerson(tx, "already@example.com", "Al", "Ready", "hash");

        int hookCalls = 0;
        QuickAccountPostCreateHook postCreate =
            [&](Transaction&, int64_t, const std::string&) { ++hookCalls; };
        QuickAccountHelper helper(db, secrets, nullptr, postCreate);

        QuickAccountResult result = helper.EnsureAccountWithWelcome(
            tx, "Different", "Name", "already@example.com");

        EXPECT_TRUE(result.alreadyExists);
        EXPECT_EQ(hookCalls, 0);
    });
}

// A throwing hook must not block or roll back the account creation — the
// "post-creation side effects can't break account creation" guarantee.
TEST(QuickAccountHelperTest, PostCreateHookExceptionDoesNotBlockAccountCreation) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("QuickAccountHookThrows", [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        auto secrets = Secrets::Test::MakeTestSecretsHelper();

        QuickAccountPostCreateHook postCreate =
            [](Transaction&, int64_t, const std::string&) {
                throw std::runtime_error("hook boom");
            };
        QuickAccountHelper helper(db, secrets, nullptr, postCreate);

        QuickAccountResult result = helper.EnsureAccountWithWelcome(
            tx, "Sturdy", "User", "sturdy@example.com");

        // Account still created despite the hook throwing.
        EXPECT_GT(result.personId, 0);
        EXPECT_FALSE(result.alreadyExists);
        TableHelpers::People people(db);
        KeyValueTable person = people.GetPersonById(tx, result.personId);
        EXPECT_EQ(person.at(std::string(DbSchema::kPeopleEmail)),
                  "sturdy@example.com");
    });
}

}  // namespace
}  // namespace Auth
