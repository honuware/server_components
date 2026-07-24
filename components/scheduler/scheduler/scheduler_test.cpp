#include "scheduler.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "scheduled_job.h"
#include "util/http/http_client_test_util.h"

namespace Scheduler {
namespace {

const char* const kServerUrl = "http://localhost:80";
const char* const kEmail = "scheduler@example.test";
const char* const kPassword = "secret";

ScheduledJob MakeJob(
    std::string name, std::string path, int64_t intervalSeconds,
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

// A small synthetic catalog. The engine is catalog-agnostic, so these tests use
// arbitrary jobs rather than the knottyyoga list (that lives in
// knottyyoga_job_catalog_test.cpp). All three jobs share one login target.
std::vector<ScheduledJob> SampleJobs() {
    return {
        MakeJob("run_billing", "/api/admin/run_billing", 86400),
        MakeJob("send_event_reminders", "/api/admin/send_event_reminders", 3600),
        MakeJob("cleanup_expired_tokens", "/api/admin/cleanup_expired_tokens", 86400),
    };
}

// Queue a successful login response on the supplied test client.
void QueueSuccessfulLogin(Http::Test::TestHttpClient& httpClient) {
    Http::HttpResponse loginResp;
    loginResp.statusCode = 200;
    loginResp.headers["Set-Cookie"] = "session_token=session-xyz";
    httpClient.QueueResponse(loginResp);
}

// One billing job per site key, each tagged with its X-Honuware-Site header so
// the engine treats it as a distinct tenant session (mirrors the control-mode
// catalog × tenants shape without pulling in the knottyyoga catalog).
std::vector<ScheduledJob> JobsForTenants(const std::vector<std::string>& sites) {
    std::vector<ScheduledJob> jobs;
    for (const auto& site : sites) {
        ScheduledJob job = MakeJob("run_billing", "/api/admin/run_billing", 86400);
        job.headers = {{"X-Honuware-Site", site}};
        jobs.push_back(std::move(job));
    }
    return jobs;
}

// A persistent handler that answers every request (login or job) with 200 + a
// session cookie — lets a test drive multiple Initialize/Refresh cycles.
void AlwaysSucceed(Http::Test::TestHttpClient& httpClient) {
    httpClient.SetResponseHandler([](const Http::HttpRequest&) {
        Http::HttpResponse resp;
        resp.statusCode = 200;
        resp.headers["Set-Cookie"] = "session_token=tok";
        return resp;
    });
}

// --- Initialize ---

TEST(SchedulerTest, InitializeLogsInWithConfiguredCredentials) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    QueueSuccessfulLogin(*httpClient);

    Scheduler scheduler(SampleJobs(), httpClient);
    EXPECT_TRUE(scheduler.Initialize());

    // All three jobs share one target, so exactly one login happens.
    ASSERT_EQ(httpClient->GetRequestCount(), 1u);
    EXPECT_EQ(httpClient->GetRequests()[0].url, "http://localhost:80/api/login");
    EXPECT_EQ(httpClient->GetRequests()[0].method, "POST");
    // The target's ApiClient stored the cookie internally.
    auto client = scheduler.GetJobScheduler().GetClientForTarget(
        kServerUrl, kEmail, kPassword);
    ASSERT_NE(client, nullptr);
    EXPECT_TRUE(client->HasCookie("session_token"));
}

TEST(SchedulerTest, InitializeReturnsFalseWhenLoginFails) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->QueueResponse(401, R"({"error":"bad creds"})");

    Scheduler scheduler(SampleJobs(), httpClient);
    EXPECT_FALSE(scheduler.Initialize());

    // Login was attempted once for the shared target.
    ASSERT_EQ(httpClient->GetRequestCount(), 1u);
}

TEST(SchedulerTest, InitializeRegistersAllEnabledJobs) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    QueueSuccessfulLogin(*httpClient);

    Scheduler scheduler(SampleJobs(), httpClient);
    ASSERT_TRUE(scheduler.Initialize());
    EXPECT_EQ(scheduler.GetJobScheduler().GetJobs().size(), 3u);
}

TEST(SchedulerTest, InitializeSkipsJobsWithZeroInterval) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    QueueSuccessfulLogin(*httpClient);

    std::vector<ScheduledJob> jobs = {
        MakeJob("enabled", "/api/admin/run_billing", 3600),
        MakeJob("disabled", "/api/admin/expire_grace_periods", 0),
        MakeJob("negative", "/api/admin/check_expiring_cards", -1),
    };
    Scheduler scheduler(std::move(jobs), httpClient);
    ASSERT_TRUE(scheduler.Initialize());
    ASSERT_EQ(scheduler.GetJobScheduler().GetJobs().size(), 1u);
    EXPECT_EQ(scheduler.GetJobScheduler().GetJobs()[0].name, "enabled");
}

// Two jobs pointing at different servers/credentials cause one login each — the
// engine handles catalog × tenants without knowing what a tenant is.
TEST(SchedulerTest, InitializeAuthenticatesEachDistinctTarget) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->SetResponseHandler([](const Http::HttpRequest&) {
        Http::HttpResponse loginResp;
        loginResp.statusCode = 200;
        loginResp.headers["Set-Cookie"] = "session_token=xyz";
        return loginResp;
    });

    std::vector<ScheduledJob> jobs = {
        MakeJob("a", "/api/admin/run_billing", 3600, "https://a.example"),
        MakeJob("b", "/api/admin/run_billing", 3600, "https://b.example"),
    };
    Scheduler scheduler(std::move(jobs), httpClient);
    ASSERT_TRUE(scheduler.Initialize());

    // Two distinct targets → two logins.
    ASSERT_EQ(httpClient->GetRequestCount(), 2u);
    EXPECT_EQ(httpClient->GetRequests()[0].url, "https://a.example/api/login");
    EXPECT_EQ(httpClient->GetRequests()[1].url, "https://b.example/api/login");
}

// --- RunFor ---

TEST(SchedulerTest, RunForExecutesEnabledJobsViaApiClient) {
    std::atomic<int> jobCallCount{0};
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->SetResponseHandler(
        [&jobCallCount](const Http::HttpRequest& req) {
            // First request is /api/login; everything else is a job firing.
            if (req.url.find("/api/login") != std::string::npos) {
                Http::HttpResponse loginResp;
                loginResp.statusCode = 200;
                loginResp.headers["Set-Cookie"] = "session_token=session-xyz";
                return loginResp;
            }
            jobCallCount.fetch_add(1, std::memory_order_relaxed);
            Http::HttpResponse jobResp;
            jobResp.statusCode = 200;
            return jobResp;
        });

    std::vector<ScheduledJob> jobs = {
        MakeJob("run_billing", "/api/admin/run_billing", 1),  // every 1s
    };
    Scheduler scheduler(std::move(jobs), httpClient);
    ASSERT_TRUE(scheduler.Initialize());
    ASSERT_EQ(scheduler.GetJobScheduler().GetJobs().size(), 1u);

    scheduler.RunFor(std::chrono::milliseconds(1500));

    EXPECT_GE(jobCallCount.load(), 1)
        << "Expected at least one billing call within 1500ms";
}

TEST(SchedulerTest, RunForIsNoOpBeforeInitialize) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    Scheduler scheduler(SampleJobs(), httpClient);
    // Explicitly do NOT call Initialize.
    scheduler.RunFor(std::chrono::milliseconds(50));
    EXPECT_EQ(httpClient->GetRequestCount(), 0u);
}

// --- Shutdown ---

TEST(SchedulerTest, ShutdownStopsTimers) {
    std::atomic<int> jobCallCount{0};
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->SetResponseHandler(
        [&jobCallCount](const Http::HttpRequest& req) {
            if (req.url.find("/api/login") != std::string::npos) {
                Http::HttpResponse loginResp;
                loginResp.statusCode = 200;
                loginResp.headers["Set-Cookie"] = "session_token=session-xyz";
                return loginResp;
            }
            jobCallCount.fetch_add(1, std::memory_order_relaxed);
            Http::HttpResponse r;
            r.statusCode = 200;
            return r;
        });

    std::vector<ScheduledJob> jobs = {
        MakeJob("run_billing", "/api/admin/run_billing", 3600),  // far future
    };
    Scheduler scheduler(std::move(jobs), httpClient);
    ASSERT_TRUE(scheduler.Initialize());

    // Shutdown before any timer fires; subsequent RunFor sees a stopped
    // io_context and unwinds immediately. No job calls should occur.
    scheduler.Shutdown();
    scheduler.RunFor(std::chrono::milliseconds(100));
    EXPECT_EQ(jobCallCount.load(), 0);
}

TEST(SchedulerTest, ShutdownIsIdempotent) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    QueueSuccessfulLogin(*httpClient);

    Scheduler scheduler(SampleJobs(), httpClient);
    ASSERT_TRUE(scheduler.Initialize());
    scheduler.Shutdown();
    scheduler.Shutdown();  // Second call must not crash.
    SUCCEED();
}

// --- Runtime structure ---

TEST(SchedulerTest, ApiClientIsConstructedWithConfiguredBaseUrl) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    httpClient->QueueResponse(200, "");  // Plain 200 — login is bool-typed.

    std::vector<ScheduledJob> jobs = {
        MakeJob("run_billing", "/api/admin/run_billing", 3600,
                "https://example.com"),
    };
    Scheduler scheduler(std::move(jobs), httpClient);
    (void)scheduler.Initialize();

    ASSERT_EQ(httpClient->GetRequestCount(), 1u);
    EXPECT_EQ(httpClient->GetRequests()[0].url,
              "https://example.com/api/login");
}

// --- Refresh (control mode: provider-driven tenant list) ---

TEST(SchedulerTest, ProviderBuildsInitialJobSetAtInitialize) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    AlwaysSucceed(*httpClient);

    JobListProvider provider = []() { return JobsForTenants({"a", "b"}); };
    Scheduler scheduler(provider, std::chrono::seconds(0), httpClient);
    ASSERT_TRUE(scheduler.Initialize());

    // Two tenants → two jobs, two logins.
    EXPECT_EQ(scheduler.GetJobScheduler().GetJobs().size(), 2u);
    EXPECT_EQ(httpClient->GetRequestCount(), 2u);
}

TEST(SchedulerTest, InitializePropagatesProviderException) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    AlwaysSucceed(*httpClient);

    JobListProvider provider = []() -> std::vector<ScheduledJob> {
        throw std::runtime_error("control db unreachable");
    };
    Scheduler scheduler(provider, std::chrono::seconds(0), httpClient);
    // main() relies on this propagating so it can exit non-zero at startup.
    EXPECT_THROW(scheduler.Initialize(), std::runtime_error);
}

TEST(SchedulerTest, RefreshWithoutProviderReturnsFalse) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    QueueSuccessfulLogin(*httpClient);
    Scheduler scheduler(SampleJobs(), httpClient);  // static ctor, no provider
    ASSERT_TRUE(scheduler.Initialize());
    EXPECT_FALSE(scheduler.Refresh());
}

TEST(SchedulerTest, RefreshPicksUpNewlyProvisionedTenant) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    AlwaysSucceed(*httpClient);

    auto sites = std::make_shared<std::vector<std::string>>(
        std::vector<std::string>{"tenant-a"});
    JobListProvider provider = [sites]() { return JobsForTenants(*sites); };

    Scheduler scheduler(provider, std::chrono::seconds(0), httpClient);
    ASSERT_TRUE(scheduler.Initialize());
    ASSERT_EQ(scheduler.GetJobScheduler().GetJobs().size(), 1u);

    // A new tenant is provisioned between refreshes.
    sites->push_back("tenant-b");
    EXPECT_TRUE(scheduler.Refresh());  // changed → reloaded
    EXPECT_EQ(scheduler.GetJobScheduler().GetJobs().size(), 2u);
    // tenant-b now has its own authenticated session.
    EXPECT_NE(scheduler.GetJobScheduler().GetClientForTarget(
                  kServerUrl, kEmail, kPassword,
                  {{"X-Honuware-Site", "tenant-b"}}),
              nullptr);
}

TEST(SchedulerTest, RefreshStopsSuspendedTenantsJobs) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    AlwaysSucceed(*httpClient);

    auto sites = std::make_shared<std::vector<std::string>>(
        std::vector<std::string>{"tenant-a", "tenant-b"});
    JobListProvider provider = [sites]() { return JobsForTenants(*sites); };

    Scheduler scheduler(provider, std::chrono::seconds(0), httpClient);
    ASSERT_TRUE(scheduler.Initialize());
    ASSERT_EQ(scheduler.GetJobScheduler().GetJobs().size(), 2u);

    // tenant-b is suspended — the provider stops returning it.
    sites->pop_back();
    EXPECT_TRUE(scheduler.Refresh());
    EXPECT_EQ(scheduler.GetJobScheduler().GetJobs().size(), 1u);
    // tenant-b's session is gone.
    EXPECT_EQ(scheduler.GetJobScheduler().GetClientForTarget(
                  kServerUrl, kEmail, kPassword,
                  {{"X-Honuware-Site", "tenant-b"}}),
              nullptr);
}

TEST(SchedulerTest, RefreshIsNoOpWhenTenantSetUnchanged) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    AlwaysSucceed(*httpClient);

    JobListProvider provider = []() { return JobsForTenants({"tenant-a"}); };
    Scheduler scheduler(provider, std::chrono::seconds(0), httpClient);
    ASSERT_TRUE(scheduler.Initialize());
    const size_t afterInit = httpClient->GetRequestCount();  // one login

    EXPECT_FALSE(scheduler.Refresh());  // unchanged → no reload
    EXPECT_EQ(scheduler.GetJobScheduler().GetJobs().size(), 1u);
    // No reload means no new login was issued.
    EXPECT_EQ(httpClient->GetRequestCount(), afterInit);
}

TEST(SchedulerTest, RefreshKeepsCurrentJobsWhenProviderThrows) {
    auto httpClient = Http::Test::MakeTestHttpClient();
    AlwaysSucceed(*httpClient);

    auto callCount = std::make_shared<int>(0);
    JobListProvider provider =
        [callCount]() -> std::vector<ScheduledJob> {
            ++(*callCount);
            if (*callCount >= 2) {
                throw std::runtime_error("control db blip");
            }
            return JobsForTenants({"tenant-a"});
        };

    Scheduler scheduler(provider, std::chrono::seconds(0), httpClient);
    ASSERT_TRUE(scheduler.Initialize());  // call 1
    ASSERT_EQ(scheduler.GetJobScheduler().GetJobs().size(), 1u);

    // The refresh's provider() call throws — a transient control-DB error must
    // not tear the scheduler down; it keeps serving the current tenants.
    EXPECT_FALSE(scheduler.Refresh());  // call 2 throws → caught
    EXPECT_EQ(scheduler.GetJobScheduler().GetJobs().size(), 1u);
}

}  // namespace
}  // namespace Scheduler
