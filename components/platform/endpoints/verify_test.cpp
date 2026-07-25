#include "verify.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cctype>
#include <iomanip>
#include <sstream>

#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "db_schema/auth_events.h"
#include "db_schema/login_attempts.h"
#include "db_schema/people.h"
#include "sql_util/table_helpers/auth_events.h"
#include "sql_util/table_helpers/login_attempts.h"
#include "sql_util/table_helpers/people.h"
#include "business_logic/auth/person.h"
#include "util/error_codes.h"
#include "util/json_value.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secret_values.h"
#include "util/mail/mail_helper_test_util.h"
#include "util/thread_pool.h"
#include "sql_util/table_helpers/email_verifications.h"
#include "business_logic/auth/person_verify_mail_test_util.h"

namespace Endpoints {
namespace {

// Phase 3.3 of the security review: verify is now POST with JSON body
// `{ email, secret }`. These helpers keep individual tests readable.
std::string BuildVerifyBody(
    std::string_view email, std::string_view encodedSecret) {
    Json::Value body(Json::JsonObject{
        {"email", std::string(email)},
        {"secret", std::string(encodedSecret)}
    });
    return body.ToString();
}

void IssueVerifyRequest(
    EndpointTestHelper& endpointHelper,
    std::string_view email,
    std::string_view encodedSecret,
    crow::response& resp) {
    crow::request req;
    req.method = crow::HTTPMethod::POST;
    req.url = "/api/verify";
    req.body = BuildVerifyBody(email, encodedSecret);
    endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
}

TEST(VerifyTest, VerifyBasic) {
    // Setup test DB + web app
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyBasic", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        auto dbh = testDb.GetDatabaseHelper();
        auto mailHelper = endpointHelper.GetMailHelper();
        auto secrets = endpointHelper.GetSecretsHelper();

        // Create a preliminarily-registered person (unverified)
        Auth::PersonHelper helper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "verify_user@example.com", "First", "Last" };
        helper.PreliminaryRegisterPerson(
            transaction, endpointHelper.GetSecretsHelper(), mailHelper, info, "SecretPw!", true);

        // Get the encoded secret from the email
        ASSERT_EQ(mailHelper->GetMessages().size(), 1u);
        const auto& msg = mailHelper->GetMessages()[0];
        std::string encodedSecret = Auth::Mail::Test::GetEncodedToken(
            secrets, info.email, info.firstName, 
            info.lastName, msg.GetBodyHtml());
        
        // Lookup id and created_at
        TableHelpers::People people(dbh);
        KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());
        EXPECT_TRUE(person.at(std::string(DbSchema::kPeopleEmailVerifiedAt)).empty());
        crow::response resp;
        IssueVerifyRequest(endpointHelper, info.email, encodedSecret, resp);

        // Phase 5.4: flush async login_attempts writer BEFORE any
        // further DB reads on the test thread. The worker shares
        // the libpqxx connection.
        ThreadPool::GetInstance().Shutdown();

        ASSERT_EQ(resp.code, 200);

        ASSERT_TRUE(helper.IsPerson(transaction, info.email));
        
        person = people.LookupPersonByEmail(transaction, info.email);
        ASSERT_TRUE(!person.empty());
        int64_t createdMicros = std::stoll(
            person.at(std::string(DbSchema::kPeopleCreatedAt)));
        int64_t verifiedMicros = std::stoll(
            person.at(std::string(DbSchema::kPeopleEmailVerifiedAt)));
        ASSERT_GE(verifiedMicros, createdMicros);

        TableHelpers::EmailVerifications emailVerifications(
            testDb.GetDatabaseHelper(), endpointHelper.GetSecretsHelper());
        try {
            emailVerifications.GetEmailVerificationInfoByEmail(
                transaction, info.email);
            FAIL() << "VerifyPersonEmailBasic - should not get here.";
        }
        catch (std::runtime_error& e) {
            ASSERT_EQ(std::string(e.what()), "EmailVerifications::GetEmailVerificationInfoByEmail - verification not found");
        }
        });
}

// --- Phase 3.3 validation tests ---

TEST(VerifyTest, VerifyMissingBody) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyMissingBody",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            crow::request req;
            req.method = crow::HTTPMethod::POST;
            req.url = "/api/verify";
            // No body.
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
            EXPECT_EQ(resp.code, 400);
        });
}

TEST(VerifyTest, VerifyMissingFieldEmail) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyMissingFieldEmail",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            Json::Value body(Json::JsonObject{
                {"secret", std::string("abc")}
            });
            crow::request req;
            req.method = crow::HTTPMethod::POST;
            req.url = "/api/verify";
            req.body = body.ToString();
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
            EXPECT_EQ(resp.code, 400);
        });
}

TEST(VerifyTest, VerifyMissingFieldSecret) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyMissingFieldSecret",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            Json::Value body(Json::JsonObject{
                {"email", std::string("user@example.com")}
            });
            crow::request req;
            req.method = crow::HTTPMethod::POST;
            req.url = "/api/verify";
            req.body = body.ToString();
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
            EXPECT_EQ(resp.code, 400);
        });
}

TEST(VerifyTest, VerifyWrongSecretReturnsGenericValidationError) {
    // Phase 3.3: failed verifications collapse to a single generic
    // 400/validation-error response — wrong secret, expired,
    // already-verified should all look the same to the caller.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyWrongSecretReturnsGenericValidationError",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            auto mailHelper = endpointHelper.GetMailHelper();

            Auth::PersonHelper helper(testDb.GetDatabaseHelper());
            Auth::PersonInfo info{
                "verify_wrong@example.com", "First", "Last" };
            helper.PreliminaryRegisterPerson(
                transaction, endpointHelper.GetSecretsHelper(),
                mailHelper, info, "SecretPw!", true);

            crow::response resp;
            IssueVerifyRequest(
                endpointHelper, info.email, "not-the-real-secret", resp);

            // Phase 5.4: flush async login_attempts writer BEFORE
            // the IsPerson DB read.
            ThreadPool::GetInstance().Shutdown();

            EXPECT_EQ(resp.code, 400);
            EXPECT_FALSE(helper.IsPerson(transaction, info.email));
        });
}

TEST(VerifyTest, VerifyNoLongerAcceptsLegacyPathParameters) {
    // Phase 3.3 regression: catch a future restoration of the URL-path
    // form that put the verification secret in the URL.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyNoLongerAcceptsLegacyPathParameters",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            crow::request req;
            req.method = crow::HTTPMethod::GET;
            req.url = "/api/verify/legacy@example.com/SECRET";
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
            EXPECT_NE(resp.code, 200);
        });
}

// Phase 5.6 of the security review: per-IP rate-limit gate on
// /api/verify. Tests that crowsynth requests don't carry a
// connection peer IP — `req.remote_ip_address` is empty by default.
// To exercise the per-IP gate without a real socket, the test sets
// `req.remote_ip_address` directly (the same field
// ResolveClientIp's fallback consults).
TEST(VerifyTest, VerifyRateLimitedPerIp) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyRateLimitedPerIp",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            auto secrets = endpointHelper.GetSecretsHelper();
            // Tight threshold so we don't insert 30 rows.
            secrets->AddSecret(transaction,
                Secrets::kAuthVerifyMaxFailuresPerIpPerWindow, "3");
            secrets->AddSecret(transaction,
                Secrets::kAuthVerifyFailureWindowInMicros,
                std::to_string(60LL * 1000000LL));

            // Pre-seed the gate's view: 3 prior verify failures from
            // this IP across various emails (per-IP bucket).
            TableHelpers::LoginAttempts attempts(testDb.GetDatabaseHelper());
            for (int i = 0; i < 3; ++i) {
                attempts.RecordAttempt(transaction,
                    "spray" + std::to_string(i) + "@example.com",
                    "10.0.0.1",
                    DbSchema::kLoginAttemptsKindVerify,
                    /*success=*/false);
            }

            // Issue a verify request that would otherwise be a
            // ValidationError ("verification failed or expired") —
            // the gate refuses it before the verification logic runs.
            Json::JsonObject body;
            body["email"] = "victim@example.com";
            body["secret"] = "any-secret";
            std::string bodyStr = Json::Value(std::move(body)).ToString();

            crow::request req;
            req.method = crow::HTTPMethod::POST;
            req.url = "/api/verify";
            req.body = bodyStr;
            req.remote_ip_address = "10.0.0.1";
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

            EXPECT_EQ(resp.code, 429);
            Json::Value respBody = Json::Value::FromText(resp.body);
            EXPECT_EQ(respBody["type"].Get<std::string>(),
                      ErrorCodes::kTooManyAttempts);

            ThreadPool::GetInstance().Shutdown();
        });
}

// Sub-threshold: a verify failure still records async and surfaces
// as the existing 400 ValidationError. Locks the contract that the
// gate's threshold can be bumped without changing the response shape.
TEST(VerifyTest, VerifyRecordsFailureAttemptAsync) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyRecordsFailureAttemptAsync",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);

            Json::JsonObject body;
            body["email"] = "rec-verify@example.com";
            body["secret"] = "totally-bogus-secret";
            std::string bodyStr = Json::Value(std::move(body)).ToString();

            crow::request req;
            req.method = crow::HTTPMethod::POST;
            req.url = "/api/verify";
            req.body = bodyStr;
            req.remote_ip_address = "10.0.0.42";
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

            EXPECT_EQ(resp.code, 400);

            ThreadPool::GetInstance().Shutdown();

            std::string countStr = transaction.RunSqlStatementReturningOneValue(
                "SELECT count(*) FROM login_attempts "
                "WHERE kind = $1 AND success = false AND ip = $2",
                std::string(DbSchema::kLoginAttemptsKindVerify),
                std::string("10.0.0.42"));
            EXPECT_EQ(countStr, "1");
        });
}

// Phase 9.1 of the security review: a successful /api/verify
// records an `email_verified` row in auth_events tied to the
// resolved person_id. The detail_json carries the verified email
// for forensic queries that filter on address.
TEST(VerifyTest, VerifySuccessRecordsAuthEvent) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifySuccessRecordsAuthEvent",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            auto dbh = testDb.GetDatabaseHelper();
            auto mailHelper = endpointHelper.GetMailHelper();
            auto secrets = endpointHelper.GetSecretsHelper();

            Auth::PersonHelper helper(dbh);
            Auth::PersonInfo info{
                "verify_audit@example.com", "First", "Last" };
            helper.PreliminaryRegisterPerson(transaction, secrets,
                mailHelper, info, "Pwd!", true);

            ASSERT_EQ(mailHelper->GetMessages().size(), 1u);
            std::string encodedSecret = Auth::Mail::Test::GetEncodedToken(
                secrets, info.email, info.firstName, info.lastName,
                mailHelper->GetMessages()[0].GetBodyHtml());

            TableHelpers::People people(dbh);
            std::string personId = people.LookupPersonByEmail(transaction,
                info.email).at(std::string(DbSchema::kPeopleId));

            crow::request req;
            req.method = crow::HTTPMethod::POST;
            req.url = "/api/verify";
            req.body = BuildVerifyBody(info.email, encodedSecret);
            req.add_header("User-Agent", "UnitTestUA/9.1");
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

            ThreadPool::GetInstance().Shutdown();
            ASSERT_EQ(resp.code, 200);

            KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
                "SELECT kind, person_id, user_agent, detail_json "
                "FROM auth_events WHERE kind = $1 "
                "ORDER BY id DESC LIMIT 1",
                std::string(DbSchema::kAuthEventsKindEmailVerified));
            EXPECT_EQ(row.at("kind"),
                std::string(DbSchema::kAuthEventsKindEmailVerified));
            EXPECT_EQ(row.at("person_id"), personId);
            EXPECT_EQ(row.at("user_agent"), "UnitTestUA/9.1");
            EXPECT_NE(row.at("detail_json").find(info.email),
                std::string::npos);
        });
}

// A failed verify (wrong secret) MUST NOT emit an email_verified
// row. The audit log is for SUCCESSFUL identity events; failure
// noise is already in login_attempts.
TEST(VerifyTest, VerifyFailureDoesNotRecordEmailVerifiedEvent) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("VerifyFailureDoesNotRecordEmailVerifiedEvent",
        [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            auto mailHelper = endpointHelper.GetMailHelper();

            Auth::PersonHelper helper(testDb.GetDatabaseHelper());
            Auth::PersonInfo info{
                "verify_failaudit@example.com", "First", "Last" };
            helper.PreliminaryRegisterPerson(transaction,
                endpointHelper.GetSecretsHelper(), mailHelper, info,
                "Pwd!", true);

            crow::response resp;
            IssueVerifyRequest(endpointHelper, info.email,
                "not-the-real-secret", resp);
            ThreadPool::GetInstance().Shutdown();
            EXPECT_EQ(resp.code, 400);

            std::string count = transaction.RunSqlStatementReturningOneValue(
                "SELECT count(*) FROM auth_events WHERE kind = $1",
                std::string(DbSchema::kAuthEventsKindEmailVerified));
            EXPECT_EQ(count, "0");
        });
}

} // namespace
} // namespace Endpoints