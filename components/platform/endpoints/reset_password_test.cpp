#include "reset_password.h"

#include <regex>

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

// Finishing website Phase 11.4. The link comes from a real /api/forgot_password
// request, read out of the mail it sends, so the two endpoints are tested
// together the way the SPA uses them. The ThreadPool is flushed after every
// request (each one queues a login_attempts insert on the test's connection).

namespace Endpoints {
namespace {

constexpr std::string_view kGenericFailure = "reset link is invalid or expired";

crow::response Post(
    EndpointTestHelper& endpointHelper, const std::string& url,
    const std::string& body, const std::string& ip = "") {
    crow::request req;
    req.method = crow::HTTPMethod::Post;
    req.url = url;
    req.body = body;
    req.remote_ip_address = ip;
    req.add_header("User-Agent", "UnitTestUA/11");
    crow::response resp;
    endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
    ThreadPool::GetInstance().Shutdown();
    return resp;
}

std::string ResetBody(
    std::string_view email, std::string_view secret, std::string_view newPassword) {
    return Json::Value(Json::JsonObject{
        {"email", std::string(email)},
        {"secret", std::string(secret)},
        {"new_password", std::string(newPassword)} }).ToString();
}

// Signs Alice up, asks for a reset the way the SPA does, and returns the
// secret from the link in the newest mail.
std::string AliceAsksForALink(
    Transaction& transaction, TestDatabaseUtil& testDb,
    EndpointTestHelper& endpointHelper) {
    Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
    if (!personHelper.IsPerson(transaction, "alice@example.com")) {
        personHelper.CreateFullyValidatedUser(
            transaction, Auth::PersonInfo{ "alice@example.com", "Alice", "Smith" },
            "old-password");
    }
    crow::response resp = Post(endpointHelper, "/api/forgot_password",
        Json::Value(Json::JsonObject{ {"email", std::string("alice@example.com")} })
            .ToString());
    EXPECT_EQ(resp.code, 200);
    const auto& messages = endpointHelper.GetMailHelper()->GetMessages();
    EXPECT_FALSE(messages.empty());
    if (messages.empty()) {
        return {};
    }
    const std::string body = messages.back().GetBodyHtml();
    std::smatch match;
    EXPECT_TRUE(std::regex_search(body, match, std::regex("secret=([A-Za-z0-9_-]+)")));
    return match.size() > 1 ? match[1].str() : std::string();
}

bool PasswordIs(Transaction& transaction, TestDatabaseUtil& testDb, std::string_view password) {
    Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
    return personHelper.VerifyPassword(transaction, "alice@example.com", password);
}

std::string Detail(const crow::response& resp) {
    return Json::Value::FromText(resp.body)["detail"].Get<std::string>();
}

TEST(ResetPasswordTest, TheLinkSetsANewPasswordAndThenStopsWorking) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetHappyPath", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        const std::string secret = AliceAsksForALink(transaction, testDb, endpointHelper);

        crow::response first = Post(endpointHelper, "/api/reset_password",
            ResetBody("alice@example.com", secret, "new-password"));
        EXPECT_EQ(first.code, 200);
        EXPECT_TRUE(PasswordIs(transaction, testDb, "new-password"));
        EXPECT_FALSE(PasswordIs(transaction, testDb, "old-password"));

        crow::response again = Post(endpointHelper, "/api/reset_password",
            ResetBody("alice@example.com", secret, "third-password"));
        EXPECT_EQ(again.code, 400);
        EXPECT_EQ(Detail(again), kGenericFailure);
        EXPECT_TRUE(PasswordIs(transaction, testDb, "new-password"));
    });
}

TEST(ResetPasswordTest, ABadSecretGetsTheGenericFailure) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetBadSecret", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        AliceAsksForALink(transaction, testDb, endpointHelper);

        crow::response wrong = Post(endpointHelper, "/api/reset_password",
            ResetBody("alice@example.com", "not-the-secret", "new-password"));
        crow::response unknown = Post(endpointHelper, "/api/reset_password",
            ResetBody("nobody@example.com", "not-the-secret", "new-password"));

        EXPECT_EQ(wrong.code, 400);
        EXPECT_EQ(Detail(wrong), kGenericFailure);
        // No account at all looks exactly the same.
        EXPECT_EQ(unknown.code, 400);
        EXPECT_EQ(unknown.body, wrong.body);
        EXPECT_TRUE(PasswordIs(transaction, testDb, "old-password"));
    });
}

TEST(ResetPasswordTest, TooManyFailuresFromOneIpGet429) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetGated", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        const std::string secret = AliceAsksForALink(transaction, testDb, endpointHelper);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(transaction,
            Secrets::kAuthPasswordResetMaxFailuresPerIpPerWindow, "3");
        TableHelpers::LoginAttempts attempts(testDb.GetDatabaseHelper());
        for (int i = 0; i < 3; ++i) {
            attempts.RecordAttempt(transaction, "n/a", "10.0.0.1",
                DbSchema::kLoginAttemptsKindPasswordReset, /*success=*/false);
        }

        // Even the right secret is refused before it is looked at.
        crow::response resp = Post(endpointHelper, "/api/reset_password",
            ResetBody("alice@example.com", secret, "new-password"), "10.0.0.1");

        EXPECT_EQ(resp.code, 429);
        EXPECT_EQ(Json::Value::FromText(resp.body)["type"].Get<std::string>(),
                  ErrorCodes::kTooManyAttempts);
        EXPECT_TRUE(PasswordIs(transaction, testDb, "old-password"));
    });
}

TEST(ResetPasswordTest, FailuresAreRecordedForTheGate) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetRecords", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        const std::string secret = AliceAsksForALink(transaction, testDb, endpointHelper);

        Post(endpointHelper, "/api/reset_password",
            ResetBody("alice@example.com", "wrong", "new-password"), "10.0.0.42");
        Post(endpointHelper, "/api/reset_password",
            ResetBody("alice@example.com", secret, "new-password"), "10.0.0.42");

        TableHelpers::LoginAttempts attempts(testDb.GetDatabaseHelper());
        // The failure counts; the success is stored but doesn't.
        EXPECT_EQ(attempts.RecentFailureCountForIp(transaction, "10.0.0.42",
                      DbSchema::kLoginAttemptsKindPasswordReset, 60LL * 1000000LL), 1);
        EXPECT_EQ(attempts.RecentAttemptCountForIp(transaction, "10.0.0.42",
                      DbSchema::kLoginAttemptsKindPasswordReset, 60LL * 1000000LL), 2);
    });
}

TEST(ResetPasswordTest, SuccessRecordsACompletedEvent) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetAuthEvent", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        const std::string secret = AliceAsksForALink(transaction, testDb, endpointHelper);
        TableHelpers::People people(testDb.GetDatabaseHelper());
        const std::string personId = people.LookupPersonByEmail(
            transaction, "alice@example.com").at("id");

        Post(endpointHelper, "/api/reset_password",
            ResetBody("alice@example.com", "wrong", "new-password"));
        Post(endpointHelper, "/api/reset_password",
            ResetBody("alice@example.com", secret, "new-password"));

        KeyValueTableArray rows = transaction.RunSqlStatementReturningKeyValueTableArray(
            "SELECT person_id, user_agent FROM auth_events WHERE kind = $1",
            std::string(DbSchema::kAuthEventsKindPasswordResetCompleted));
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(rows[0].at("person_id"), personId);
        EXPECT_EQ(rows[0].at("user_agent"), "UnitTestUA/11");
    });
}

TEST(ResetPasswordTest, SuccessSignsOutEverySession) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetSignsOut", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        const std::string secret = AliceAsksForALink(transaction, testDb, endpointHelper);
        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string session;
        ASSERT_TRUE(personHelper.CreateSessionToken(
            transaction, endpointHelper.GetSecretsHelper(), "alice@example.com", session));
        // What session restore asks; a deleted session throws "not found".
        auto signsIn = [&]() {
            int64_t personId = 0;
            try {
                return personHelper.LookupPersonIdBySessionToken(
                    transaction, session, personId);
            } catch (const std::exception&) {
                return false;
            }
        };
        ASSERT_TRUE(signsIn());

        ASSERT_EQ(Post(endpointHelper, "/api/reset_password",
                       ResetBody("alice@example.com", secret, "new-password")).code,
                  200);

        EXPECT_FALSE(signsIn());
    });
}

TEST(ResetPasswordTest, MissingFieldsAre400) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ResetMissingFields", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        const std::string url = "/api/reset_password";
        EXPECT_EQ(Post(endpointHelper, url, "").code, 400);
        EXPECT_EQ(Post(endpointHelper, url, "{}").code, 400);
        EXPECT_EQ(Post(endpointHelper, url, ResetBody("", "s", "p")).code, 400);
        EXPECT_EQ(Post(endpointHelper, url, ResetBody("a@example.com", "", "p")).code, 400);
        crow::response noPassword =
            Post(endpointHelper, url, ResetBody("a@example.com", "s", ""));
        EXPECT_EQ(noPassword.code, 400);
        EXPECT_EQ(Detail(noPassword), "new_password is required");
    });
}

}  // namespace
}  // namespace Endpoints
