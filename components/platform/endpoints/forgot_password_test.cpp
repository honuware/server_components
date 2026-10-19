#include "forgot_password.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "business_logic/auth/person.h"
#include "db_schema/auth_events.h"
#include "db_schema/login_attempts.h"
#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "sql_util/table_helpers/login_attempts.h"
#include "sql_util/table_helpers/people.h"
#include "util/error_codes.h"
#include "util/json_value.h"
#include "util/mail/mail_helper_test_util.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/thread_pool.h"

// Finishing website Phase 11.4. Every request flushes the ThreadPool right
// after handle_full: the endpoint queues the mail AND a login_attempts insert,
// and the insert runs on the test's own connection.

namespace Endpoints {
namespace {

using ::testing::HasSubstr;

crow::response PostForgotPassword(
    EndpointTestHelper& endpointHelper, const std::string& body,
    const std::string& ip = "") {
    crow::request req;
    req.method = crow::HTTPMethod::Post;
    req.url = "/api/forgot_password";
    req.body = body;
    req.remote_ip_address = ip;
    req.add_header("User-Agent", "UnitTestUA/11");
    crow::response resp;
    endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
    ThreadPool::GetInstance().Shutdown();
    return resp;
}

std::string EmailBody(std::string_view email) {
    return Json::Value(Json::JsonObject{ {"email", std::string(email)} }).ToString();
}

void AddPerson(Transaction& transaction, TestDatabaseUtil& testDb, std::string_view email) {
    Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
    personHelper.CreateFullyValidatedUser(
        transaction, Auth::PersonInfo{ std::string(email), "Alice", "Smith" }, "pw");
}

TEST(ForgotPasswordTest, AKnownEmailGets200AndOneMail) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ForgotKnown", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        AddPerson(transaction, testDb, "alice@example.com");

        crow::response resp = PostForgotPassword(
            endpointHelper, EmailBody("alice@example.com"));

        EXPECT_EQ(resp.code, 200);
        ASSERT_EQ(endpointHelper.GetMailHelper()->GetMessages().size(), 1u);
        const ::Mail::MailMessage& message =
            endpointHelper.GetMailHelper()->GetMessages()[0];
        EXPECT_EQ(message.GetTo()[0].address, "alice@example.com");
        EXPECT_THAT(message.GetBodyHtml(),
                    HasSubstr("reset-password?email=alice%40example.com&secret="));
    });
}

TEST(ForgotPasswordTest, AnUnknownEmailGets200AndNoMail) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ForgotUnknown", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        crow::response resp = PostForgotPassword(
            endpointHelper, EmailBody("nobody@example.com"));

        EXPECT_EQ(resp.code, 200);
        EXPECT_TRUE(endpointHelper.GetMailHelper()->GetMessages().empty());
    });
}

TEST(ForgotPasswordTest, TheAnswerIsTheSameWhetherOrNotTheAccountExists) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ForgotSameAnswer", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        AddPerson(transaction, testDb, "alice@example.com");

        crow::response known = PostForgotPassword(
            endpointHelper, EmailBody("alice@example.com"));
        crow::response unknown = PostForgotPassword(
            endpointHelper, EmailBody("nobody@example.com"));

        EXPECT_EQ(known.code, unknown.code);
        EXPECT_EQ(known.body, unknown.body);
    });
}

TEST(ForgotPasswordTest, TooManyRequestsForOneAddressGet429AndNoMail) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ForgotGated", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        AddPerson(transaction, testDb, "alice@example.com");
        TableHelpers::LoginAttempts attempts(testDb.GetDatabaseHelper());
        for (int i = 0; i < 5; ++i) {
            attempts.RecordAttempt(transaction, "alice@example.com",
                "10.0.0." + std::to_string(i),
                DbSchema::kLoginAttemptsKindPasswordResetRequest, /*success=*/true);
        }

        crow::response resp = PostForgotPassword(
            endpointHelper, EmailBody("alice@example.com"), "203.0.113.9");

        EXPECT_EQ(resp.code, 429);
        EXPECT_EQ(Json::Value::FromText(resp.body)["type"].Get<std::string>(),
                  ErrorCodes::kTooManyAttempts);
        EXPECT_TRUE(endpointHelper.GetMailHelper()->GetMessages().empty());
    });
}

TEST(ForgotPasswordTest, TooManyRequestsFromOneIpGet429) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ForgotGatedIp", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        TableHelpers::LoginAttempts attempts(testDb.GetDatabaseHelper());
        for (int i = 0; i < 20; ++i) {
            attempts.RecordAttempt(transaction,
                "spray" + std::to_string(i) + "@example.com", "10.0.0.1",
                DbSchema::kLoginAttemptsKindPasswordResetRequest, /*success=*/true);
        }

        crow::response resp = PostForgotPassword(
            endpointHelper, EmailBody("fresh@example.com"), "10.0.0.1");

        EXPECT_EQ(resp.code, 429);
    });
}

TEST(ForgotPasswordTest, EveryRequestCountsTowardTheCaps) {
    // Known address or not: the cap is on requests.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ForgotRecords", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        AddPerson(transaction, testDb, "alice@example.com");

        PostForgotPassword(endpointHelper, EmailBody("alice@example.com"), "10.0.0.7");
        PostForgotPassword(endpointHelper, EmailBody("nobody@example.com"), "10.0.0.7");

        TableHelpers::LoginAttempts attempts(testDb.GetDatabaseHelper());
        const int64_t hourUs = 3600LL * 1000000LL;
        EXPECT_EQ(attempts.RecentAttemptCountForIp(transaction, "10.0.0.7",
                      DbSchema::kLoginAttemptsKindPasswordResetRequest, hourUs), 2);
        EXPECT_EQ(attempts.RecentAttemptCountForEmail(transaction, "nobody@example.com",
                      DbSchema::kLoginAttemptsKindPasswordResetRequest, hourUs), 1);
    });
}

TEST(ForgotPasswordTest, AKnownEmailRecordsARequestedEvent) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ForgotAuthEvent", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        AddPerson(transaction, testDb, "alice@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        const std::string personId = people.LookupPersonByEmail(
            transaction, "alice@example.com").at("id");

        PostForgotPassword(endpointHelper, EmailBody("alice@example.com"));
        PostForgotPassword(endpointHelper, EmailBody("nobody@example.com"));

        KeyValueTableArray rows = transaction.RunSqlStatementReturningKeyValueTableArray(
            "SELECT person_id, user_agent FROM auth_events WHERE kind = $1",
            std::string(DbSchema::kAuthEventsKindPasswordResetRequested));
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(rows[0].at("person_id"), personId);
        EXPECT_EQ(rows[0].at("user_agent"), "UnitTestUA/11");
    });
}

TEST(ForgotPasswordTest, AMissingEmailIs400) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ForgotMissingEmail", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        EXPECT_EQ(PostForgotPassword(endpointHelper, "{}").code, 400);
        EXPECT_EQ(PostForgotPassword(endpointHelper, EmailBody("")).code, 400);
        EXPECT_EQ(PostForgotPassword(endpointHelper, "").code, 400);
    });
}

}  // namespace
}  // namespace Endpoints
