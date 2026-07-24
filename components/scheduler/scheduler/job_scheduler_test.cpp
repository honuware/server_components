#include "job_scheduler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>

#include "api_client.h"
#include "util/http/http_client_test_util.h"

namespace Scheduler {
namespace {

const char* const kServerUrl = "http://localhost:80";
const char* const kEmail = "scheduler@example.test";
const char* const kPassword = "secret";

ScheduledJob MakeJob(
    std::string name, std::string path, int64_t intervalSeconds = 60,
    std::string serverUrl = kServerUrl) {
    ScheduledJob job;
    job.name = std::move(name);
    job.method = "POST";
    job.path = std::move(path);
    job.intervalSeconds = intervalSeconds;
    job.serverUrl = std::move(serverUrl);
    job.serviceAccountEmail = kEmail;
    job.serviceAccountPassword = kPassword;
    return job;
}

// --- RunJobOnce ---
// RunJobOnce lazily creates the target's ApiClient WITHOUT logging in, so these
// tests see exactly one request per RunJobOnce (no login) unless they call
// AuthenticateTargets first.

TEST(JobSchedulerTest, RunJobOnceCallsApiClientWithRightMethodAndPath) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->SetResponseHandler([](const Http::HttpRequest&) {
        Http::HttpResponse response;
        response.statusCode = 200;
        response.body = R"({"total_expired":0})";
        return response;
    });

    JobScheduler scheduler(httpClient);
    scheduler.RegisterJob(MakeJob("expire_grace", "/api/admin/expire_grace_periods"));

    auto result = scheduler.RunJobOnce("expire_grace");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.statusCode, 200);
    EXPECT_EQ(result.jobName, "expire_grace");
    EXPECT_TRUE(result.errorMessage.empty());

    ASSERT_EQ(httpClient->GetRequestCount(), 1u);
    EXPECT_EQ(httpClient->GetRequests()[0].method, "POST");
    EXPECT_EQ(httpClient->GetRequests()[0].url,
              "http://localhost:80/api/admin/expire_grace_periods");
}

// Per-job headers are attached to the outgoing request — the mechanism the
// tenant plan relies on for headers like an origin secret or a site header.
TEST(JobSchedulerTest, RunJobOnceAttachesPerJobHeaders) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->QueueResponse(200, "");

    JobScheduler scheduler(httpClient);
    ScheduledJob job = MakeJob("billing", "/api/admin/run_billing");
    job.headers = {{"X-Origin-Secret", "shhh"}, {"X-Honuware-Site", "tenant-a"}};
    scheduler.RegisterJob(std::move(job));

    scheduler.RunJobOnce("billing");

    ASSERT_EQ(httpClient->GetRequestCount(), 1u);
    const auto& req = httpClient->GetRequests()[0];
    auto originSecret = req.headers.find("X-Origin-Secret");
    ASSERT_NE(originSecret, req.headers.end());
    EXPECT_EQ(originSecret->second, "shhh");
    auto site = req.headers.find("X-Honuware-Site");
    ASSERT_NE(site, req.headers.end());
    EXPECT_EQ(site->second, "tenant-a");
}

TEST(JobSchedulerTest, RunJobOnceMarksFailureOn4xx) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->QueueResponse(403, R"({"error":"forbidden"})");

    JobScheduler scheduler(httpClient);
    scheduler.RegisterJob(MakeJob("billing", "/api/admin/run_billing"));

    auto result = scheduler.RunJobOnce("billing");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.statusCode, 403);
    EXPECT_TRUE(result.errorMessage.empty());
}

TEST(JobSchedulerTest, RunJobOnceMarksFailureOn5xx) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->QueueResponse(500, "internal error");

    JobScheduler scheduler(httpClient);
    scheduler.RegisterJob(MakeJob("billing", "/api/admin/run_billing"));

    auto result = scheduler.RunJobOnce("billing");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.statusCode, 500);
}

TEST(JobSchedulerTest, RunJobOnceCapturesExceptions) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->QueueError("simulated network failure");

    JobScheduler scheduler(httpClient);
    scheduler.RegisterJob(MakeJob("billing", "/api/admin/run_billing"));

    // The exception must NOT propagate out of RunJobOnce — the timer loop
    // depends on this so a single broken endpoint doesn't crash the helper.
    auto result = scheduler.RunJobOnce("billing");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.statusCode, 0);
    EXPECT_NE(result.errorMessage.find("simulated network failure"),
              std::string::npos);
}

TEST(JobSchedulerTest, RunJobOnceUnknownJobThrows) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    JobScheduler scheduler(httpClient);

    EXPECT_THROW(scheduler.RunJobOnce("nope"), std::out_of_range);
}

// --- RegisterJob ---

TEST(JobSchedulerTest, RegisterJobSkipsDisabled) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    JobScheduler scheduler(httpClient);

    scheduler.RegisterJob(MakeJob("enabled", "/api/admin/run_billing", 60));
    scheduler.RegisterJob(MakeJob("disabled", "/api/admin/expire_grace_periods", 0));
    scheduler.RegisterJob(MakeJob("negative", "/api/admin/run_billing", -1));

    ASSERT_EQ(scheduler.GetJobs().size(), 1u);
    EXPECT_EQ(scheduler.GetJobs()[0].name, "enabled");
}

// --- AuthenticateTargets ---

TEST(JobSchedulerTest, AuthenticateTargetsLogsInOncePerDistinctTarget) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->SetResponseHandler([](const Http::HttpRequest&) {
        Http::HttpResponse resp;
        resp.statusCode = 200;
        resp.headers["Set-Cookie"] = "session_token=tok";
        return resp;
    });

    JobScheduler scheduler(httpClient);
    // Two jobs share target A; one job uses target B.
    scheduler.RegisterJob(MakeJob("a1", "/api/admin/run_billing", 60, "https://a.example"));
    scheduler.RegisterJob(MakeJob("a2", "/api/admin/expire_grace_periods", 60, "https://a.example"));
    scheduler.RegisterJob(MakeJob("b1", "/api/admin/run_billing", 60, "https://b.example"));

    EXPECT_TRUE(scheduler.AuthenticateTargets());

    // Two distinct targets → exactly two logins.
    ASSERT_EQ(httpClient->GetRequestCount(), 2u);
    EXPECT_EQ(httpClient->GetRequests()[0].url, "https://a.example/api/login");
    EXPECT_EQ(httpClient->GetRequests()[1].url, "https://b.example/api/login");

    auto clientA = scheduler.GetClientForTarget("https://a.example", kEmail, kPassword);
    ASSERT_NE(clientA, nullptr);
    EXPECT_TRUE(clientA->HasCookie("session_token"));
}

TEST(JobSchedulerTest, AuthenticateTargetsReturnsFalseIfAnyLoginFails) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->SetResponseHandler([](const Http::HttpRequest& req) {
        Http::HttpResponse resp;
        // Target B's login fails.
        resp.statusCode =
            (req.url.find("b.example") != std::string::npos) ? 401 : 200;
        if (resp.statusCode == 200) resp.headers["Set-Cookie"] = "session_token=tok";
        return resp;
    });

    JobScheduler scheduler(httpClient);
    scheduler.RegisterJob(MakeJob("a1", "/api/admin/run_billing", 60, "https://a.example"));
    scheduler.RegisterJob(MakeJob("b1", "/api/admin/run_billing", 60, "https://b.example"));

    EXPECT_FALSE(scheduler.AuthenticateTargets());
    // Both targets were attempted.
    EXPECT_EQ(httpClient->GetRequestCount(), 2u);
}

// After AuthenticateTargets, RunJobOnce reuses the logged-in client and sends
// its session cookie rather than re-authenticating.
TEST(JobSchedulerTest, RunJobOnceReusesAuthenticatedSession) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    // 1: login, 2: job call.
    Http::HttpResponse loginResp;
    loginResp.statusCode = 200;
    loginResp.headers["Set-Cookie"] = "session_token=abc";
    httpClient->QueueResponse(loginResp);
    httpClient->QueueResponse(200, R"({"ok":true})");

    JobScheduler scheduler(httpClient);
    scheduler.RegisterJob(MakeJob("billing", "/api/admin/run_billing"));
    ASSERT_TRUE(scheduler.AuthenticateTargets());

    auto result = scheduler.RunJobOnce("billing");
    EXPECT_TRUE(result.success);

    ASSERT_EQ(httpClient->GetRequestCount(), 2u);  // login + job, no re-login
    const auto& jobReq = httpClient->GetRequests()[1];
    auto cookie = jobReq.headers.find("Cookie");
    ASSERT_NE(cookie, jobReq.headers.end());
    EXPECT_EQ(cookie->second, "session_token=abc");
}

// --- Per-tenant sessions (control mode: same origin/creds, different site) ---

namespace {
std::string ReqHeader(const Http::HttpRequest& req, const std::string& name) {
    auto it = req.headers.find(name);
    return it == req.headers.end() ? std::string() : it->second;
}
}  // namespace

// The control-mode shape: every tenant shares one origin URL, one service
// account email, and one password — distinguished ONLY by X-Honuware-Site. The
// engine must NOT collapse them into a single session (that would log in once
// and reuse one tenant's cookie for every tenant); each distinct site header is
// its own login target.
TEST(JobSchedulerTest, DistinctSiteHeadersLogInPerTenant) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->SetResponseHandler([](const Http::HttpRequest&) {
        Http::HttpResponse resp;
        resp.statusCode = 200;
        resp.headers["Set-Cookie"] = "session_token=tok";
        return resp;
    });

    JobScheduler scheduler(httpClient);
    ScheduledJob a = MakeJob("a", "/api/admin/run_billing");
    a.headers = {{"X-Honuware-Site", "tenant-a"}};
    ScheduledJob b = MakeJob("b", "/api/admin/run_billing");
    b.headers = {{"X-Honuware-Site", "tenant-b"}};
    scheduler.RegisterJob(std::move(a));
    scheduler.RegisterJob(std::move(b));

    EXPECT_TRUE(scheduler.AuthenticateTargets());

    // Two tenants → two logins (not one collapsed login).
    ASSERT_EQ(httpClient->GetRequestCount(), 2u);
    // Each login carried its own tenant's site header (order not assumed).
    std::vector<std::string> sites;
    for (const auto& req : httpClient->GetRequests()) {
        EXPECT_EQ(req.url, "http://localhost:80/api/login");
        sites.push_back(ReqHeader(req, "X-Honuware-Site"));
    }
    EXPECT_NE(std::find(sites.begin(), sites.end(), "tenant-a"), sites.end());
    EXPECT_NE(std::find(sites.begin(), sites.end(), "tenant-b"), sites.end());

    // Distinct per-tenant clients; the header-less lookup matches neither.
    auto clientA = scheduler.GetClientForTarget(
        kServerUrl, kEmail, kPassword, {{"X-Honuware-Site", "tenant-a"}});
    auto clientB = scheduler.GetClientForTarget(
        kServerUrl, kEmail, kPassword, {{"X-Honuware-Site", "tenant-b"}});
    ASSERT_NE(clientA, nullptr);
    ASSERT_NE(clientB, nullptr);
    EXPECT_NE(clientA, clientB);
    EXPECT_EQ(scheduler.GetClientForTarget(kServerUrl, kEmail, kPassword),
              nullptr);
}

// Two jobs for the SAME tenant (same site header) share one login/session.
TEST(JobSchedulerTest, SameTenantJobsShareOneSession) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->SetResponseHandler([](const Http::HttpRequest&) {
        Http::HttpResponse resp;
        resp.statusCode = 200;
        resp.headers["Set-Cookie"] = "session_token=tok";
        return resp;
    });

    JobScheduler scheduler(httpClient);
    ScheduledJob j1 = MakeJob("j1", "/api/admin/run_billing");
    j1.headers = {{"X-Honuware-Site", "tenant-a"}};
    ScheduledJob j2 = MakeJob("j2", "/api/admin/expire_grace_periods");
    j2.headers = {{"X-Honuware-Site", "tenant-a"}};
    scheduler.RegisterJob(std::move(j1));
    scheduler.RegisterJob(std::move(j2));

    EXPECT_TRUE(scheduler.AuthenticateTargets());
    EXPECT_EQ(httpClient->GetRequestCount(), 1u);  // one shared login
}

// SetJobs (control-mode refresh) replaces the whole set, drops disabled jobs,
// and clears the session cache so a vanished tenant loses its client.
TEST(JobSchedulerTest, SetJobsReplacesJobSetAndClearsSessions) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->SetResponseHandler([](const Http::HttpRequest&) {
        Http::HttpResponse resp;
        resp.statusCode = 200;
        resp.headers["Set-Cookie"] = "session_token=tok";
        return resp;
    });

    JobScheduler scheduler(httpClient);
    ScheduledJob a = MakeJob("a", "/api/admin/run_billing");
    a.headers = {{"X-Honuware-Site", "tenant-a"}};
    scheduler.RegisterJob(std::move(a));
    ASSERT_TRUE(scheduler.AuthenticateTargets());
    ASSERT_NE(scheduler.GetClientForTarget(
                  kServerUrl, kEmail, kPassword,
                  {{"X-Honuware-Site", "tenant-a"}}),
              nullptr);

    // Refresh to a different tenant set; include a disabled job to prove it's
    // dropped.
    std::vector<ScheduledJob> next;
    ScheduledJob b = MakeJob("b", "/api/admin/run_billing");
    b.headers = {{"X-Honuware-Site", "tenant-b"}};
    next.push_back(std::move(b));
    ScheduledJob off = MakeJob("off", "/api/admin/run_billing", 0);
    off.headers = {{"X-Honuware-Site", "tenant-b"}};
    next.push_back(std::move(off));
    scheduler.SetJobs(std::move(next));

    ASSERT_EQ(scheduler.GetJobs().size(), 1u);
    EXPECT_EQ(scheduler.GetJobs()[0].name, "b");
    // Old tenant-a session dropped; new tenant-b not yet authenticated.
    EXPECT_EQ(scheduler.GetClientForTarget(
                  kServerUrl, kEmail, kPassword,
                  {{"X-Honuware-Site", "tenant-a"}}),
              nullptr);
    EXPECT_EQ(scheduler.GetClientForTarget(
                  kServerUrl, kEmail, kPassword,
                  {{"X-Honuware-Site", "tenant-b"}}),
              nullptr);
    ASSERT_TRUE(scheduler.AuthenticateTargets());
    EXPECT_NE(scheduler.GetClientForTarget(
                  kServerUrl, kEmail, kPassword,
                  {{"X-Honuware-Site", "tenant-b"}}),
              nullptr);
}

// --- Async timer firing ---

TEST(JobSchedulerTest, StartFiresJobsOnInterval) {
    // Smallest interval is 1 second (seconds resolution matches production);
    // run_for(1500ms) reliably catches at least one firing.
    std::atomic<int> requestCount{0};
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->SetResponseHandler(
        [&requestCount](const Http::HttpRequest&) {
        requestCount.fetch_add(1, std::memory_order_relaxed);
        Http::HttpResponse response;
        response.statusCode = 200;
        return response;
    });

    JobScheduler scheduler(httpClient);
    scheduler.RegisterJob(MakeJob("fast", "/api/admin/run_billing", 1));

    boost::asio::io_context ioContext;
    scheduler.Start(ioContext);
    ioContext.run_for(std::chrono::milliseconds(1500));
    scheduler.Stop();

    EXPECT_GE(requestCount.load(), 1)
        << "Expected at least one execution within 1500ms with a 1s interval";
}

TEST(JobSchedulerTest, StopCancelsScheduledJobs) {
    std::atomic<int> requestCount{0};
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->SetResponseHandler(
        [&requestCount](const Http::HttpRequest&) {
        requestCount.fetch_add(1, std::memory_order_relaxed);
        Http::HttpResponse response;
        response.statusCode = 200;
        return response;
    });

    JobScheduler scheduler(httpClient);
    scheduler.RegisterJob(MakeJob("hourly", "/api/admin/run_billing", 3600));

    boost::asio::io_context ioContext;
    scheduler.Start(ioContext);
    // The job is scheduled for 3600s out — so calling Stop and running the
    // io_context briefly should NOT execute the job.
    scheduler.Stop();
    ioContext.run_for(std::chrono::milliseconds(50));

    EXPECT_EQ(requestCount.load(), 0);
}

TEST(JobSchedulerTest, StartIsIdempotent) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    JobScheduler scheduler(httpClient);
    scheduler.RegisterJob(MakeJob("hourly", "/api/admin/run_billing", 3600));

    boost::asio::io_context ioContext;
    scheduler.Start(ioContext);
    // Calling Start again with a job already scheduled must not double-up
    // timers or crash.
    scheduler.Start(ioContext);
    scheduler.Stop();
    SUCCEED();
}

}  // namespace
}  // namespace Scheduler
