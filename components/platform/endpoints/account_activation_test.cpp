#include "account_activation.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/person_verify_mail_test_util.h"
#include "db_schema/people.h"
#include "db_schema/role_assignments.h"
#include "db_schema/roles.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "sql_util/table_helpers/roles.h"
#include "util/error_codes.h"
#include "util/json_value.h"
#include "util/mail/mail_helper_test_util.h"
#include "util/mail/mail_helper.h"

namespace Endpoints {
namespace {

TEST(AccountActivationTest, AccountActivationBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AccountActivationBasic", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        Mail::Test::TestMailHelperPtr mailHelper = endpointHelper.GetMailHelper();
        auto secrets = endpointHelper.GetSecretsHelper();

        // Create a preliminarily-registered person (unverified)
        Auth::PersonHelper helper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "activate_user@example.com", "First", "Last" };
        helper.PreliminaryRegisterPerson(
            transaction,
            endpointHelper.GetSecretsHelper(),
            std::static_pointer_cast<::Mail::MailHelper>(mailHelper),
            info,
            "SecretPw!",
            true);

        // Extract the encoded token from the email body
        ASSERT_EQ(mailHelper->GetMessages().size(), 1u);
        const auto& msg = mailHelper->GetMessages()[0];
        std::string encodedToken = Auth::Mail::Test::GetEncodedToken(
            secrets,
            info.email,
            info.firstName,
            info.lastName,
            msg.GetBodyHtml());

        ASSERT_FALSE(helper.IsPerson(transaction, info.email));

        // Call endpoint
        crow::request req;
        req.method = crow::HTTPMethod::GET;
        req.url = "/api/account_activation/" + info.email + "/" + encodedToken;
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        ASSERT_EQ(resp.code, 302);
        EXPECT_TRUE(helper.IsPerson(transaction, info.email));
    });
}

TEST(AccountActivationTest, AccountActivationEmailNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AccountActivationEmailNotFound", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        crow::request req;
        req.method = crow::HTTPMethod::GET;
        req.url = "/api/account_activation/missing@example.com/ABC123";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 400);

        // Verify JSON error response format
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
        EXPECT_EQ(body["status"].Get<int64_t>(), 400);
    });
}

TEST(AccountActivationTest, AccountActivationInvalidToken) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AccountActivationInvalidToken", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        Mail::Test::TestMailHelperPtr mailHelper = endpointHelper.GetMailHelper();
        auto secrets = endpointHelper.GetSecretsHelper();

        Auth::PersonHelper helper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "activate_invalid@example.com", "First", "Last" };
        helper.PreliminaryRegisterPerson(
            transaction,
            endpointHelper.GetSecretsHelper(),
            std::static_pointer_cast<::Mail::MailHelper>(mailHelper),
            info,
            "SecretPw!",
            true);

        ASSERT_FALSE(helper.IsPerson(transaction, info.email));

        crow::request req;
        req.method = crow::HTTPMethod::GET;
        req.url = "/api/account_activation/" + info.email + "/INVALID_TOKEN";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 400);

        // Verify JSON error response format
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
        EXPECT_EQ(body["status"].Get<int64_t>(), 400);

        EXPECT_FALSE(helper.IsPerson(transaction, info.email));
    });
}

// Phase 3.5 of the security review: a previous implementation granted
// the admin role to anyone whose firstName was "Mason" or "Tyler" the
// moment they completed account activation. That was a privilege-
// escalation path the moment the rule leaked. This test pins the
// removal — registering as "Mason" or "Tyler" must NOT confer admin.
TEST(AccountActivationTest, AccountActivationDoesNotGrantAdminByName) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AccountActivationDoesNotGrantAdminByName",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            Mail::Test::TestMailHelperPtr mailHelper = endpointHelper.GetMailHelper();
            auto secrets = endpointHelper.GetSecretsHelper();

            // Make sure the admin role exists so we can compare against
            // its id below. (CreateAndPopulateDatabases would seed it in
            // production, but tests use a fresh schema with no rows.)
            TableHelpers::Roles roles(testDb.GetDatabaseHelper());
            int64_t adminRoleId = roles.AddRole(
                transaction,
                std::string(DbSchema::kRoleNameAdmin),
                "Administrator");

            Auth::PersonHelper helper(testDb.GetDatabaseHelper());
            Auth::PersonInfo info{
                "mason-imposter@example.com", "Mason", "Imposter" };
            helper.PreliminaryRegisterPerson(
                transaction,
                secrets,
                std::static_pointer_cast<::Mail::MailHelper>(mailHelper),
                info,
                "SecretPw!",
                true);

            ASSERT_EQ(mailHelper->GetMessages().size(), 1u);
            const auto& msg = mailHelper->GetMessages()[0];
            std::string encodedToken = Auth::Mail::Test::GetEncodedToken(
                secrets, info.email, info.firstName, info.lastName,
                msg.GetBodyHtml());

            crow::request req;
            req.method = crow::HTTPMethod::GET;
            req.url = "/api/account_activation/" + info.email + "/" + encodedToken;
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
            ASSERT_EQ(resp.code, 302);

            // The user is verified — but they must NOT be an admin.
            EXPECT_TRUE(helper.IsPerson(transaction, info.email));

            TableHelpers::People people(testDb.GetDatabaseHelper());
            int64_t personId = std::stoll(
                people.LookupPersonByEmail(transaction, info.email)
                    .at(std::string(DbSchema::kPeopleId)));

            TableHelpers::RoleAssignments ra(testDb.GetDatabaseHelper());
            KeyValueTableArray assignments = ra.GetRoleAssignmentsForPerson(
                transaction, personId);
            for (const auto& assignment : assignments) {
                int64_t roleId = std::stoll(
                    assignment.at(
                        std::string(DbSchema::kRoleAssignmentsRoleId)));
                EXPECT_NE(roleId, adminRoleId)
                    << "Phase 3.5 regression: a person registered with "
                    << "firstName=Mason was granted the admin role.";
            }
        });
}

TEST(AccountActivationTest, AccountActivationDoesNotGrantAdminByNameTyler) {
    // Companion regression for the second hardcoded name. Same test
    // shape; the duplication is deliberate so a future refactor that
    // accidentally re-introduces "Tyler" is also caught.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AccountActivationDoesNotGrantAdminByNameTyler",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            Mail::Test::TestMailHelperPtr mailHelper = endpointHelper.GetMailHelper();
            auto secrets = endpointHelper.GetSecretsHelper();

            TableHelpers::Roles roles(testDb.GetDatabaseHelper());
            int64_t adminRoleId = roles.AddRole(
                transaction,
                std::string(DbSchema::kRoleNameAdmin),
                "Administrator");

            Auth::PersonHelper helper(testDb.GetDatabaseHelper());
            Auth::PersonInfo info{
                "tyler-imposter@example.com", "Tyler", "Imposter" };
            helper.PreliminaryRegisterPerson(
                transaction,
                secrets,
                std::static_pointer_cast<::Mail::MailHelper>(mailHelper),
                info,
                "SecretPw!",
                true);

            ASSERT_EQ(mailHelper->GetMessages().size(), 1u);
            const auto& msg = mailHelper->GetMessages()[0];
            std::string encodedToken = Auth::Mail::Test::GetEncodedToken(
                secrets, info.email, info.firstName, info.lastName,
                msg.GetBodyHtml());

            crow::request req;
            req.method = crow::HTTPMethod::GET;
            req.url = "/api/account_activation/" + info.email + "/" + encodedToken;
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
            ASSERT_EQ(resp.code, 302);
            EXPECT_TRUE(helper.IsPerson(transaction, info.email));

            TableHelpers::People people(testDb.GetDatabaseHelper());
            int64_t personId = std::stoll(
                people.LookupPersonByEmail(transaction, info.email)
                    .at(std::string(DbSchema::kPeopleId)));

            TableHelpers::RoleAssignments ra(testDb.GetDatabaseHelper());
            KeyValueTableArray assignments = ra.GetRoleAssignmentsForPerson(
                transaction, personId);
            for (const auto& assignment : assignments) {
                int64_t roleId = std::stoll(
                    assignment.at(
                        std::string(DbSchema::kRoleAssignmentsRoleId)));
                EXPECT_NE(roleId, adminRoleId)
                    << "Phase 3.5 regression: a person registered with "
                    << "firstName=Tyler was granted the admin role.";
            }
        });
}

} // namespace
} // namespace Endpoints
