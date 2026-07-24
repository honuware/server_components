#include "api_client.h"

#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>

#include <gtest/gtest.h>

#include "util/http/http_client_test_util.h"
#include "util/json_value.h"

namespace Scheduler {
namespace {

// RAII stdout capture. LogInfo()/LogError() write to *g_logStream which
// defaults to &std::cout, so swapping cout's rdbuf gives us a clean way to
// assert that a particular log line was emitted. Restoration happens in
// the destructor so a thrown EXPECT can't leak the redirect.
class CoutCapture {
public:
    CoutCapture() : original_(std::cout.rdbuf(buffer_.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(original_); }
    CoutCapture(const CoutCapture&) = delete;
    CoutCapture& operator=(const CoutCapture&) = delete;
    std::string str() const { return buffer_.str(); }
private:
    std::stringstream buffer_;
    std::streambuf* original_;
};

// --- BuildUrl ---

TEST(ApiClientTest, BuildUrlJoinsBaseAndPath) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->QueueResponse(200, "");
    ApiClient client(httpClient, "http://localhost:80");

    client.CallEndpoint("GET", "/api/admin/run_billing");

    ASSERT_EQ(httpClient->GetRequestCount(), 1u);
    EXPECT_EQ(httpClient->GetRequests()[0].url,
              "http://localhost:80/api/admin/run_billing");
}

TEST(ApiClientTest, BuildUrlStripsTrailingSlashFromBase) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->QueueResponse(200, "");
    ApiClient client(httpClient, "http://localhost:80/");

    client.CallEndpoint("GET", "/api/health");

    ASSERT_EQ(httpClient->GetRequestCount(), 1u);
    EXPECT_EQ(httpClient->GetRequests()[0].url,
              "http://localhost:80/api/health");
}

// --- Login ---

TEST(ApiClientTest, LoginSendsCorrectRequestAndStoresCookie) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    Http::HttpResponse response;
    response.statusCode = 200;
    response.headers["Set-Cookie"] = "session_token=abc123; Path=/; HttpOnly";
    httpClient->QueueResponse(response);

    ApiClient client(httpClient, "http://localhost:80");
    bool ok = client.Login("scheduler@knottyyoga.local", "secret");
    EXPECT_TRUE(ok);

    ASSERT_EQ(httpClient->GetRequestCount(), 1u);
    const auto& req = httpClient->GetRequests()[0];
    EXPECT_EQ(req.url, "http://localhost:80/api/login");
    EXPECT_EQ(req.method, "POST");

    Json::Value parsedBody = Json::Value::FromText(req.body);
    EXPECT_EQ(parsedBody["email"].Get<std::string>(),
              "scheduler@knottyyoga.local");
    EXPECT_EQ(parsedBody["password"].Get<std::string>(), "secret");
    EXPECT_EQ(parsedBody["remember"].Get<bool>(), false);

    auto headerIt = req.headers.find("Content-Type");
    ASSERT_NE(headerIt, req.headers.end());
    EXPECT_EQ(headerIt->second, "application/json");

    EXPECT_TRUE(client.HasCookie("session_token"));
    EXPECT_EQ(client.GetCookieValue("session_token"), "abc123");
}

TEST(ApiClientTest, LoginReturnsFalseOnNon200) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->QueueResponse(401, R"({"error":"bad creds"})");

    ApiClient client(httpClient, "http://localhost:80");
    EXPECT_FALSE(client.Login("scheduler@knottyyoga.local", "wrong"));
    EXPECT_EQ(client.GetCookieCount(), 0u);
}

TEST(ApiClientTest, LoginAcceptsLowercaseSetCookieHeader) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    Http::HttpResponse response;
    response.statusCode = 200;
    response.headers["set-cookie"] = "session_token=lowercased";
    httpClient->QueueResponse(response);

    ApiClient client(httpClient, "http://localhost:80");
    EXPECT_TRUE(client.Login("u@example.com", "p"));
    EXPECT_EQ(client.GetCookieValue("session_token"), "lowercased");
}

TEST(ApiClientTest, LoginIgnoresMalformedSetCookie) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    Http::HttpResponse response;
    response.statusCode = 200;
    response.headers["Set-Cookie"] = "no-equals-sign";
    httpClient->QueueResponse(response);

    ApiClient client(httpClient, "http://localhost:80");
    EXPECT_TRUE(client.Login("u@example.com", "p"));
    EXPECT_EQ(client.GetCookieCount(), 0u);
}

TEST(ApiClientTest, LoginClearsPriorCookies) {
    auto httpClient = Http::Test::MakeTestHttpClient();

    // First login: get a session cookie.
    Http::HttpResponse first;
    first.statusCode = 200;
    first.headers["Set-Cookie"] = "session_token=first";
    httpClient->QueueResponse(first);

    // Second login: server returns a non-200 with no Set-Cookie. We should
    // have cleared the prior cookie regardless.
    httpClient->QueueResponse(401, "");

    ApiClient client(httpClient, "http://localhost:80");
    ASSERT_TRUE(client.Login("u@example.com", "p"));
    EXPECT_EQ(client.GetCookieValue("session_token"), "first");

    EXPECT_FALSE(client.Login("u@example.com", "newpass"));
    EXPECT_EQ(client.GetCookieCount(), 0u);
}

// --- CallEndpoint cookie attachment ---

TEST(ApiClientTest, CallEndpointAttachesCookieAfterLogin) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    // Login response with cookie.
    Http::HttpResponse loginResp;
    loginResp.statusCode = 200;
    loginResp.headers["Set-Cookie"] = "session_token=abc; Path=/";
    httpClient->QueueResponse(loginResp);
    // Subsequent call response.
    httpClient->QueueResponse(200, R"({"reminders_sent":0})");

    ApiClient client(httpClient, "http://localhost:80");
    ASSERT_TRUE(client.Login("u@example.com", "p"));
    Http::HttpResponse resp = client.CallEndpoint(
        "POST", "/api/admin/send_event_reminders");
    EXPECT_EQ(resp.statusCode, 200);

    ASSERT_EQ(httpClient->GetRequestCount(), 2u);
    const auto& adminReq = httpClient->GetRequests()[1];
    auto cookieIt = adminReq.headers.find("Cookie");
    ASSERT_NE(cookieIt, adminReq.headers.end());
    EXPECT_EQ(cookieIt->second, "session_token=abc");
}

TEST(ApiClientTest, CallEndpointSendsNoCookieHeaderWhenNotLoggedIn) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->QueueResponse(200, "");

    ApiClient client(httpClient, "http://localhost:80");
    client.CallEndpoint("GET", "/api/health");

    ASSERT_EQ(httpClient->GetRequestCount(), 1u);
    EXPECT_EQ(httpClient->GetRequests()[0].headers.find("Cookie"),
              httpClient->GetRequests()[0].headers.end());
}

TEST(ApiClientTest, CallEndpointSendsBodyAndContentType) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->QueueResponse(200, "");

    ApiClient client(httpClient, "http://localhost:80");
    client.CallEndpoint("POST", "/api/admin/run_billing", R"({"force":true})");

    ASSERT_EQ(httpClient->GetRequestCount(), 1u);
    const auto& req = httpClient->GetRequests()[0];
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.body, R"({"force":true})");
    auto ct = req.headers.find("Content-Type");
    ASSERT_NE(ct, req.headers.end());
    EXPECT_EQ(ct->second, "application/json");
}

// --- Extra (per-job) headers ---

TEST(ApiClientTest, CallEndpointAttachesExtraHeaders) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->QueueResponse(200, "");

    ApiClient client(httpClient, "http://localhost:80");
    client.CallEndpoint("POST", "/api/admin/run_billing", "",
        {{"X-Origin-Secret", "shhh"}, {"X-Honuware-Site", "tenant-a"}});

    ASSERT_EQ(httpClient->GetRequestCount(), 1u);
    const auto& req = httpClient->GetRequests()[0];
    auto originSecret = req.headers.find("X-Origin-Secret");
    ASSERT_NE(originSecret, req.headers.end());
    EXPECT_EQ(originSecret->second, "shhh");
    auto site = req.headers.find("X-Honuware-Site");
    ASSERT_NE(site, req.headers.end());
    EXPECT_EQ(site->second, "tenant-a");
}

TEST(ApiClientTest, ExtraHeadersSurviveThe401Retry) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    // Login → admin call (401) → re-login → retry (200).
    Http::HttpResponse login1;
    login1.statusCode = 200;
    login1.headers["Set-Cookie"] = "session_token=old";
    httpClient->QueueResponse(login1);
    httpClient->QueueResponse(401, "");
    Http::HttpResponse login2;
    login2.statusCode = 200;
    login2.headers["Set-Cookie"] = "session_token=new";
    httpClient->QueueResponse(login2);
    httpClient->QueueResponse(200, R"({"ok":true})");

    ApiClient client(httpClient, "http://localhost:80");
    ASSERT_TRUE(client.Login("u@example.com", "p"));
    client.CallEndpoint("POST", "/api/admin/run_billing", "",
        {{"X-Origin-Secret", "shhh"}});

    ASSERT_EQ(httpClient->GetRequestCount(), 4u);
    // Both the initial admin call (index 1) and the retried one (index 3)
    // carry the per-job header.
    auto first = httpClient->GetRequests()[1].headers.find("X-Origin-Secret");
    ASSERT_NE(first, httpClient->GetRequests()[1].headers.end());
    EXPECT_EQ(first->second, "shhh");
    auto retried = httpClient->GetRequests()[3].headers.find("X-Origin-Secret");
    ASSERT_NE(retried, httpClient->GetRequests()[3].headers.end());
    EXPECT_EQ(retried->second, "shhh");
}

// --- Login headers (tenant site / origin secret) ---

// The client's constructor headers ride on the /api/login request itself. In
// control mode the server resolves which tenant DB to authenticate against from
// X-Honuware-Site, so a header-less login is rejected before credentials are
// even checked.
TEST(ApiClientTest, LoginSendsConstructorLoginHeaders) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    Http::HttpResponse response;
    response.statusCode = 200;
    response.headers["Set-Cookie"] = "session_token=abc";
    httpClient->QueueResponse(response);

    ApiClient client(httpClient, "http://localhost:80",
        {{"X-Honuware-Site", "tenant-a"}, {"X-Origin-Secret", "shhh"}});
    ASSERT_TRUE(client.Login("scheduler@knottyyoga.local", "secret"));

    ASSERT_EQ(httpClient->GetRequestCount(), 1u);
    const auto& req = httpClient->GetRequests()[0];
    EXPECT_EQ(req.url, "http://localhost:80/api/login");
    auto site = req.headers.find("X-Honuware-Site");
    ASSERT_NE(site, req.headers.end());
    EXPECT_EQ(site->second, "tenant-a");
    auto secret = req.headers.find("X-Origin-Secret");
    ASSERT_NE(secret, req.headers.end());
    EXPECT_EQ(secret->second, "shhh");
    // Content-Type is still set (login headers are applied on top, not instead).
    EXPECT_NE(req.headers.find("Content-Type"), req.headers.end());
}

TEST(ApiClientTest, LoginSendsNoTenantHeadersByDefault) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    Http::HttpResponse response;
    response.statusCode = 200;
    response.headers["Set-Cookie"] = "session_token=abc";
    httpClient->QueueResponse(response);

    ApiClient client(httpClient, "http://localhost:80");  // no login headers
    ASSERT_TRUE(client.Login("u@example.com", "p"));

    const auto& req = httpClient->GetRequests()[0];
    EXPECT_EQ(req.headers.find("X-Honuware-Site"), req.headers.end());
}

// The re-login triggered by a 401 must ALSO carry the login headers, or the
// re-auth would be rejected by the tenant guard and the retry would fail.
TEST(ApiClientTest, LoginHeadersRideThe401Relogin) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    Http::HttpResponse login1;
    login1.statusCode = 200;
    login1.headers["Set-Cookie"] = "session_token=old";
    httpClient->QueueResponse(login1);
    httpClient->QueueResponse(401, "");
    Http::HttpResponse login2;
    login2.statusCode = 200;
    login2.headers["Set-Cookie"] = "session_token=new";
    httpClient->QueueResponse(login2);
    httpClient->QueueResponse(200, R"({"ok":true})");

    ApiClient client(httpClient, "http://localhost:80",
        {{"X-Honuware-Site", "tenant-b"}});
    ASSERT_TRUE(client.Login("u@example.com", "p"));
    client.CallEndpoint("POST", "/api/admin/run_billing", "",
        {{"X-Honuware-Site", "tenant-b"}});

    ASSERT_EQ(httpClient->GetRequestCount(), 4u);
    // Index 2 is the 401-triggered re-login — assert it hit /api/login WITH the
    // tenant site header.
    const auto& relogin = httpClient->GetRequests()[2];
    EXPECT_EQ(relogin.url, "http://localhost:80/api/login");
    auto site = relogin.headers.find("X-Honuware-Site");
    ASSERT_NE(site, relogin.headers.end());
    EXPECT_EQ(site->second, "tenant-b");
}

// --- 401 retry semantics ---

TEST(ApiClientTest, CallEndpointRetriesOnce401AfterRelogin) {
    auto httpClient = Http::Test::MakeTestHttpClient();

    // Initial login succeeds with cookie "old".
    Http::HttpResponse login1;
    login1.statusCode = 200;
    login1.headers["Set-Cookie"] = "session_token=old";
    httpClient->QueueResponse(login1);

    // First admin call: 401 (stale cookie).
    httpClient->QueueResponse(401, "");

    // Re-login succeeds with fresh cookie "new".
    Http::HttpResponse login2;
    login2.statusCode = 200;
    login2.headers["Set-Cookie"] = "session_token=new";
    httpClient->QueueResponse(login2);

    // Retry succeeds with the new cookie.
    httpClient->QueueResponse(200, R"({"ok":true})");

    ApiClient client(httpClient, "http://localhost:80");
    ASSERT_TRUE(client.Login("u@example.com", "p"));

    Http::HttpResponse resp = client.CallEndpoint(
        "POST", "/api/admin/run_billing");

    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_EQ(resp.body, R"({"ok":true})");

    // 4 requests total: login, admin (401), login, admin (200)
    ASSERT_EQ(httpClient->GetRequestCount(), 4u);

    // First admin call sent the old cookie.
    auto cookie1 = httpClient->GetRequests()[1].headers.find("Cookie");
    ASSERT_NE(cookie1, httpClient->GetRequests()[1].headers.end());
    EXPECT_EQ(cookie1->second, "session_token=old");

    // Second login is at /api/login.
    EXPECT_EQ(httpClient->GetRequests()[2].url,
              "http://localhost:80/api/login");

    // Retried admin call sent the new cookie.
    auto cookie2 = httpClient->GetRequests()[3].headers.find("Cookie");
    ASSERT_NE(cookie2, httpClient->GetRequests()[3].headers.end());
    EXPECT_EQ(cookie2->second, "session_token=new");
}

TEST(ApiClientTest, CallEndpointReturns401WhenReloginAlsoFails) {
    auto httpClient = Http::Test::MakeTestHttpClient();

    // Initial login succeeds.
    Http::HttpResponse login1;
    login1.statusCode = 200;
    login1.headers["Set-Cookie"] = "session_token=old";
    httpClient->QueueResponse(login1);

    // Admin call: 401.
    httpClient->QueueResponse(401, R"({"error":"unauthorized"})");

    // Re-login fails (e.g., password rotated).
    httpClient->QueueResponse(401, "");

    ApiClient client(httpClient, "http://localhost:80");
    ASSERT_TRUE(client.Login("u@example.com", "p"));

    Http::HttpResponse resp = client.CallEndpoint(
        "POST", "/api/admin/run_billing");

    // No further retry — the original 401 surfaces (or in this case the
    // failed re-login left us with the original 401 response).
    EXPECT_EQ(resp.statusCode, 401);
    // 3 requests total: login, admin (401), login (401). No fourth attempt.
    EXPECT_EQ(httpClient->GetRequestCount(), 3u);
}

TEST(ApiClientTest, CallEndpointDoesNotRetryWithoutCachedCredentials) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->QueueResponse(401, "");

    ApiClient client(httpClient, "http://localhost:80");
    // No Login() call — no credentials cached.
    Http::HttpResponse resp = client.CallEndpoint(
        "POST", "/api/admin/run_billing");

    EXPECT_EQ(resp.statusCode, 401);
    // Just the one attempt — no retry, since we have nothing to log in with.
    EXPECT_EQ(httpClient->GetRequestCount(), 1u);
}

TEST(ApiClientTest, CallEndpointDoesNotRetryOnNon401) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    Http::HttpResponse loginResp;
    loginResp.statusCode = 200;
    loginResp.headers["Set-Cookie"] = "session_token=ok";
    httpClient->QueueResponse(loginResp);
    httpClient->QueueResponse(500, "internal error");

    ApiClient client(httpClient, "http://localhost:80");
    ASSERT_TRUE(client.Login("u@example.com", "p"));

    Http::HttpResponse resp = client.CallEndpoint(
        "POST", "/api/admin/run_billing");
    EXPECT_EQ(resp.statusCode, 500);
    // login + admin call only; no retry on 5xx.
    EXPECT_EQ(httpClient->GetRequestCount(), 2u);
}

// --- Phase 11 logging ---

TEST(ApiClientTest, LoginSuccessEmitsStructuredLog) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    Http::HttpResponse loginResp;
    loginResp.statusCode = 200;
    loginResp.headers["Set-Cookie"] = "session_token=abc";
    httpClient->QueueResponse(loginResp);

    std::string captured;
    {
        CoutCapture capture;
        ApiClient client(httpClient, "http://localhost:80");
        EXPECT_TRUE(client.Login("scheduler@knottyyoga.local", "secret"));
        captured = capture.str();
    }

    EXPECT_NE(captured.find("[api_client] event=login_success"),
              std::string::npos)
        << "captured: " << captured;
    EXPECT_NE(captured.find("email=scheduler@knottyyoga.local"),
              std::string::npos);
    EXPECT_NE(captured.find("status=200"), std::string::npos);
    EXPECT_NE(captured.find("cookies=1"), std::string::npos);
}

TEST(ApiClientTest, LoginFailureEmitsStructuredLog) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->QueueResponse(401, "");

    std::string captured;
    {
        CoutCapture capture;
        ApiClient client(httpClient, "http://localhost:80");
        EXPECT_FALSE(client.Login("scheduler@knottyyoga.local", "wrong"));
        captured = capture.str();
    }

    EXPECT_NE(captured.find("[api_client] event=login_failure"),
              std::string::npos)
        << "captured: " << captured;
    EXPECT_NE(captured.find("status=401"), std::string::npos);
}

TEST(ApiClientTest, ReauthEmitsStructuredLog) {
    // Login → admin call → 401 → re-auth → retry succeeds.
    auto httpClient = Http::Test::MakeTestHttpClient();
    Http::HttpResponse login1;
    login1.statusCode = 200;
    login1.headers["Set-Cookie"] = "session_token=old";
    httpClient->QueueResponse(login1);
    httpClient->QueueResponse(401, "");
    Http::HttpResponse login2;
    login2.statusCode = 200;
    login2.headers["Set-Cookie"] = "session_token=new";
    httpClient->QueueResponse(login2);
    httpClient->QueueResponse(200, R"({"ok":true})");

    std::string captured;
    {
        CoutCapture capture;
        ApiClient client(httpClient, "http://localhost:80");
        ASSERT_TRUE(client.Login("u@example.com", "p"));
        Http::HttpResponse resp = client.CallEndpoint(
            "POST", "/api/admin/run_billing");
        EXPECT_EQ(resp.statusCode, 200);
        captured = capture.str();
    }

    EXPECT_NE(captured.find("[api_client] event=reauth_required"),
              std::string::npos)
        << "captured: " << captured;
    EXPECT_NE(captured.find("path=/api/admin/run_billing"),
              std::string::npos);
    EXPECT_NE(captured.find("method=POST"), std::string::npos);
    // Two login_success lines: initial + re-auth.
    size_t firstSuccess = captured.find("event=login_success");
    ASSERT_NE(firstSuccess, std::string::npos);
    EXPECT_NE(captured.find("event=login_success", firstSuccess + 1),
              std::string::npos)
        << "expected two login_success lines (initial + re-auth)";
}

}  // namespace
}  // namespace Scheduler
