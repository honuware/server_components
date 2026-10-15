#include <gtest/gtest.h>

#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "endpoints/web_app.h"
#include "business_logic/auth/cookie_manager.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "scheduler/api_client.h"
#include "util/http/http_client.h"
#include "util/json_value.h"
#include "util/mail/mail_helper_test_util.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/thread_pool.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/test_transaction_provider.h"

namespace Endpoints {
namespace {

// Runs one transaction at a time. The server's handler threads and the
// ThreadPool's async writes (login_attempts) all land on the test's ONE
// connection, which is not thread-safe; in production
// ProductionTransactionProvider's own mutex gives the same guarantee.
// Recursive, in case a handler opens a transaction inside another.
class SerializedTransactionProvider : public TransactionProvider {
public:
    explicit SerializedTransactionProvider(TransactionProviderPtr inner)
        : inner_(std::move(inner)) {}

    void RunInTransaction(DatabaseFunc databaseFunc) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        inner_->RunInTransaction(std::move(databaseFunc));
    }

private:
    TransactionProviderPtr inner_;
    std::recursive_mutex mutex_;
};

// Finishing website Phase 4.4 -- the scheduler helper talking to a REAL server.
//
// From 10/8/2026 every scheduled job was refused by CsrfGuard with 403 ("csrft
// cookie missing"). /api/login sets two cookies, the session cookie and csrft;
// the libcurl client kept only the last Set-Cookie, and ApiClient never sent
// X-CSRF-Token. Every piece had its own passing tests: the client against a
// double, the guard against hand-built requests, login against the test cookie
// manager. The bug lived only where they meet.
//
// Why this is a live-server test (http_client_test.cpp asks each one to make
// its own case): no in-process test can reach that seam. Crow's handle_full()
// skips the global middleware, so CookieParser never turns cookies into
// Set-Cookie headers and CsrfGuard never runs -- which is why EndpointTestHelper
// uses a test cookie manager and switches the guard off. Only a request through
// a real socket runs the middleware and the libcurl header parsing together.
//
// The server shares the test's transaction (TestTransactionProvider), so nothing
// it writes outlives the test. Requests are sequential -- the test thread is
// blocked in curl while a Crow worker runs -- and SerializedTransactionProvider
// keeps /api/login's async write off the connection while a request uses it.
//
// The last step is the one production needs after every session expiry: the
// helper's call gets 401, it logs in again on its own, and the retry succeeds.
// Until Phase 4.4 that re-login sent a garbled email (ApiClient::Login read
// its own freed buffer), so the helper could never have recovered.
TEST(ApiClientEndToEndTest, SchedulerClientPassesTheCsrfGuardAfterARealLogin) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ApiClientEndToEnd", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL)); // 15 minutes
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL)); // 7 days
        // login 401s on the success path if kWebsiteAddress is empty.
        secrets->AddSecret(transaction, Secrets::kWebsiteAddress, "example.test");

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        Auth::PersonInfo info{ "scheduler_end_to_end@example.com", "Scheduled", "Jobs" };
        personHelper.CreateFullyValidatedUser(transaction, info, "MyPassword!");

        // A WebApp as main.cpp builds one: the REAL cookie manager (cookies
        // become Set-Cookie headers through crow::CookieParser) and CsrfGuard
        // left on, as it is by default.
        WebApp webApp(
            testDb.GetDatabaseHelper(),
            testDb.GetDatabaseInfo().GetDatabaseName(),
            testDb.GetDatabaseInfo(),
            secrets,
            Mail::Test::MakeTestMailHelper(),
            std::make_shared<SerializedTransactionProvider>(
                MakeTestTransactionProvider(transaction)),
            Auth::MakeCookieManagerFactory());
        RoutingBase::AddRoutes(webApp);
        ASSERT_TRUE(webApp.GetApp().get_middleware<CsrfGuard>().IsEnabled());

        // Port 0 on loopback, read back after start -- see
        // http_client_test.cpp for why.
        auto& app = webApp.GetApp();
        std::future<void> serverDone =
            app.bindaddr("127.0.0.1").port(0).run_async();
        app.wait_for_server_start();
        const std::uint16_t port = app.port();

        const std::string baseUrl = "http://127.0.0.1:" + std::to_string(port);
        Http::HttpClientPtr httpClient = Http::MakeHttpClient();

        // Everything is collected first and asserted after the server stops,
        // so a failed expectation cannot leave it running.
        Http::HttpResponse bareResponse;
        bool loggedIn = false;
        bool hasSessionCookie = false;
        bool hasCsrfCookie = false;
        Http::HttpResponse mismatchedResponse;
        Http::HttpResponse userInfoResponse;
        Http::HttpResponse recoveredResponse;
        std::string sessionBefore;
        std::string sessionAfter;
        if (port != 0) {
            // 1. The guard is live on this path: a POST without the token is
            //    refused. (Through handle_full() it would not even run.)
            Http::HttpRequest bare;
            bare.url = baseUrl + "/api/get_user_info";
            bare.method = "POST";
            bare.body = "{}";
            bare.headers["Content-Type"] = "application/json";
            bareResponse = httpClient->Execute(bare);

            // 2. The helper logs in, as scheduler/main.cpp does.
            Scheduler::ApiClient client(httpClient, baseUrl);
            loggedIn = client.Login(info.email, "MyPassword!");
            ThreadPool::Shutdown();
            hasSessionCookie = client.HasCookie("session_token");
            hasCsrfCookie = client.HasCookie("csrft");

            // 3. A token that does not match the csrft cookie is refused.
            mismatchedResponse = client.CallEndpoint(
                "POST", "/api/get_user_info", "{}",
                {{"X-CSRF-Token", "not-the-cookie-value"}});

            // 4. The helper's own request passes the guard AND is signed in --
            //    the session cookie and the csrft cookie both made the trip.
            userInfoResponse = client.CallEndpoint(
                "POST", "/api/get_user_info", "{}");

            // 5. The session ends, as it does when it expires. Logout passes
            //    the guard too; the helper keeps its now-dead cookie. Its next
            //    call gets 401, it logs in again, and the retry succeeds.
            sessionBefore = client.GetCookieValue("session_token");
            client.CallEndpoint("POST", "/api/logout", "{}");
            recoveredResponse = client.CallEndpoint(
                "POST", "/api/get_user_info", "{}");
            sessionAfter = client.GetCookieValue("session_token");
            ThreadPool::Shutdown();
        }

        app.stop();
        serverDone.wait();

        ASSERT_NE(port, 0) << "server did not report a bound port";

        EXPECT_EQ(bareResponse.statusCode, 403);
        EXPECT_NE(bareResponse.body.find("csrf_token_missing_or_invalid"),
                  std::string::npos)
            << "body: " << bareResponse.body;

        EXPECT_TRUE(loggedIn);
        EXPECT_TRUE(hasSessionCookie);
        EXPECT_TRUE(hasCsrfCookie);

        EXPECT_EQ(mismatchedResponse.statusCode, 403);

        ASSERT_EQ(userInfoResponse.statusCode, 200)
            << "body: " << userInfoResponse.body;
        Json::Value userInfo = Json::Value::FromText(userInfoResponse.body);
        EXPECT_EQ(userInfo["email"].Get<std::string>(), info.email);

        // A new session, not the old one surviving the logout.
        EXPECT_FALSE(sessionAfter.empty());
        EXPECT_NE(sessionAfter, sessionBefore);
        ASSERT_EQ(recoveredResponse.statusCode, 200)
            << "body: " << recoveredResponse.body;
        Json::Value recovered = Json::Value::FromText(recoveredResponse.body);
        EXPECT_EQ(recovered["email"].Get<std::string>(), info.email);
    });

    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
