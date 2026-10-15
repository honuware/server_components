#include "health.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "endpoints/endpoint_test_helper.h"
#include "web_app.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/tenancy/tenant_resources.h"
#include "sql_util/database_access/transaction_provider.h"
#include "test/src/util/database_test_helper.h"
#include "util/json_value.h"

namespace Endpoints {
namespace {

// --- Helpers ---

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, /*overwrite=*/1);
#endif
}

void UnsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// The version env var the health endpoint reads, plus its legacy fallback.
constexpr const char* kVersionEnvVars[] = {
    "HONUWARE_VERSION",
    "KNOTTYYOGA_VERSION",
};

// RAII guard that clears BOTH the canonical HONUWARE_VERSION and the legacy
// KNOTTYYOGA_VERSION on entry (so each test starts hermetic) and restores their
// prior values on exit. Tests can mutate them freely without affecting siblings.
class VersionEnvScope {
public:
    VersionEnvScope() {
        for (const char* name : kVersionEnvVars) {
            const char* val = std::getenv(name);
            saved_.emplace_back(
                name, val ? std::optional<std::string>(val) : std::nullopt);
            UnsetEnv(name);
        }
    }
    ~VersionEnvScope() {
        for (const auto& entry : saved_) {
            if (entry.second) SetEnv(entry.first, entry.second->c_str());
            else UnsetEnv(entry.first);
        }
    }
private:
    std::vector<std::pair<const char*, std::optional<std::string>>> saved_;
};

// Test-only TransactionProvider that always throws. Used to drive the
// DB-failure path through GetHealth without having to take down a real DB.
class ThrowingTransactionProvider : public TransactionProvider {
public:
    void RunInTransaction(DatabaseFunc) override {
        throw std::runtime_error("simulated DB failure");
    }
};

// Test-only TransactionProvider whose probe succeeds without a database: it
// accepts the work and runs nothing.
class AcceptingTransactionProvider : public TransactionProvider {
public:
    void RunInTransaction(DatabaseFunc) override {}
};

std::vector<std::string> FailedNames(const Json::Value& body) {
    std::vector<std::string> names;
    const Json::Value* failed = nullptr;
    if (body.HasChild("failed", &failed)) {
        for (const Json::Value& name : failed->GetArray()) {
            names.push_back(name.Get<std::string>());
        }
    }
    return names;
}

// A tenant registry whose every pool is DEAD, standing in for 10/15/2026: the
// site's pooled connection had been dropped while the global one still worked.
std::shared_ptr<Tenancy::TenantResourceRegistry> DeadPoolRegistry() {
    return std::make_shared<Tenancy::TenantResourceRegistry>(
        [](const Tenancy::TenantContext& context) {
            auto resources = std::make_shared<Tenancy::TenantResources>();
            resources->context = context;
            resources->transactionProvider =
                std::make_shared<ThrowingTransactionProvider>();
            return resources;
        });
}

// --- BuildHealthResponse: pure JSON shape ---

TEST(HealthTest, BuildHealthResponseOk) {
    Json::Value body = BuildHealthResponse(true, "v1.0.0-sandbox.3");
    EXPECT_EQ(body["status"].Get<std::string>(), "ok");
    EXPECT_EQ(body["db"].Get<std::string>(), "ok");
    EXPECT_EQ(body["version"].Get<std::string>(), "v1.0.0-sandbox.3");
}

TEST(HealthTest, BuildHealthResponseFail) {
    Json::Value body = BuildHealthResponse(false, "v1.0.0-sandbox.3");
    EXPECT_EQ(body["status"].Get<std::string>(), "fail");
    EXPECT_EQ(body["db"].Get<std::string>(), "fail");
    EXPECT_EQ(body["version"].Get<std::string>(), "v1.0.0-sandbox.3");
}

TEST(HealthTest, BuildHealthResponseEmptyVersion) {
    Json::Value body = BuildHealthResponse(true, "");
    EXPECT_EQ(body["version"].Get<std::string>(), "");
}

// A healthy body is exactly what it always was, so the deploy script's and
// UptimeRobot's checks are unaffected.
TEST(HealthTest, BuildHealthResponseHealthyBodyHasNoFailedList) {
    Json::Value body = BuildHealthResponse(true, "v1");
    EXPECT_FALSE(body.HasChild("failed", nullptr));
}

TEST(HealthTest, BuildHealthResponseNamesWhatFailed) {
    Json::Value body = BuildHealthResponse(false, "v1", {"knottyyoga"});
    EXPECT_EQ(body["status"].Get<std::string>(), "fail");
    EXPECT_EQ(FailedNames(body), std::vector<std::string>{"knottyyoga"});
}

// --- GetBuildVersion: env var handling ---

TEST(HealthTest, GetBuildVersionFromEnv) {
    VersionEnvScope scope;
    SetEnv("HONUWARE_VERSION", "v2026.04.27-abcdef");
    EXPECT_EQ(GetBuildVersion(), "v2026.04.27-abcdef");
}

TEST(HealthTest, GetBuildVersionUnsetReturnsUnknown) {
    VersionEnvScope scope;
    EXPECT_EQ(GetBuildVersion(), "unknown");
}

TEST(HealthTest, GetBuildVersionEmptyReturnsUnknown) {
    VersionEnvScope scope;
    SetEnv("HONUWARE_VERSION", "");
    EXPECT_EQ(GetBuildVersion(), "unknown");
}

TEST(HealthTest, GetBuildVersionLegacyFallback) {
    // Old deploys that set only the legacy KNOTTYYOGA_VERSION keep working.
    VersionEnvScope scope;
    SetEnv("KNOTTYYOGA_VERSION", "v2026.04.27-legacy");
    EXPECT_EQ(GetBuildVersion(), "v2026.04.27-legacy");
}

TEST(HealthTest, GetBuildVersionCanonicalWinsOverLegacy) {
    // When both are set, the canonical HONUWARE_VERSION wins.
    VersionEnvScope scope;
    SetEnv("HONUWARE_VERSION", "v-canonical");
    SetEnv("KNOTTYYOGA_VERSION", "v-legacy");
    EXPECT_EQ(GetBuildVersion(), "v-canonical");
}

// --- ProbeDatabase ---

TEST(HealthTest, ProbeDatabaseNullProviderReturnsFalse) {
    EXPECT_FALSE(ProbeDatabase(nullptr));
}

TEST(HealthTest, ProbeDatabaseThrowingProviderReturnsFalse) {
    auto throwingProvider = std::make_shared<ThrowingTransactionProvider>();
    EXPECT_FALSE(ProbeDatabase(throwingProvider));
}

// --- GetHealth: function-level ---

TEST(HealthTest, GetHealthDbOkSets200) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HealthGetHealthOk", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        crow::request req;
        crow::response resp;
        Json::Value body = GetHealth(
            endpointHelper.GetWebApp().GetTransactionProvider(), req, resp);

        EXPECT_EQ(resp.code, 200);
        EXPECT_EQ(body["status"].Get<std::string>(), "ok");
        EXPECT_EQ(body["db"].Get<std::string>(), "ok");
        EXPECT_TRUE(body.HasChild("version", nullptr));
    });

    Auth::ServerConfig::Shutdown();
}

TEST(HealthTest, GetHealthDbFailSets503) {
    auto throwingProvider = std::make_shared<ThrowingTransactionProvider>();

    crow::request req;
    crow::response resp;
    Json::Value body = GetHealth(throwingProvider, req, resp);

    EXPECT_EQ(resp.code, 503);
    EXPECT_EQ(body["status"].Get<std::string>(), "fail");
    EXPECT_EQ(body["db"].Get<std::string>(), "fail");
    EXPECT_TRUE(body.HasChild("version", nullptr));
}

TEST(HealthTest, GetHealthNullProviderSets503) {
    crow::request req;
    crow::response resp;
    Json::Value body = GetHealth(/*transactionProvider=*/nullptr, req, resp);

    EXPECT_EQ(resp.code, 503);
    EXPECT_EQ(body["status"].Get<std::string>(), "fail");
}

// --- GetHealth over several connections (Finishing website Phase 4.2) ---

TEST(HealthTest, GetHealthProbesEveryTargetAndNamesTheFailures) {
    auto accepting = std::make_shared<AcceptingTransactionProvider>();
    std::vector<HealthProbeTarget> targets{
        {"global", accepting},
        {"knottyyoga", std::make_shared<ThrowingTransactionProvider>()},
        {"other_studio", accepting},
    };

    crow::request req;
    crow::response resp;
    Json::Value body = GetHealth(targets, req, resp);

    EXPECT_EQ(resp.code, 503);
    EXPECT_EQ(body["db"].Get<std::string>(), "fail");
    EXPECT_EQ(FailedNames(body), std::vector<std::string>{"knottyyoga"});
}

TEST(HealthTest, GetHealthAllTargetsHealthySets200) {
    auto accepting = std::make_shared<AcceptingTransactionProvider>();
    crow::request req;
    crow::response resp;
    Json::Value body = GetHealth(
        std::vector<HealthProbeTarget>{{"global", accepting}, {"knottyyoga", accepting}},
        req, resp);

    EXPECT_EQ(resp.code, 200);
    EXPECT_FALSE(body.HasChild("failed", nullptr));
}

// Nothing probed proves nothing, so it is not "ok".
TEST(HealthTest, GetHealthWithNoTargetsIsNotHealthy) {
    crow::request req;
    crow::response resp;
    GetHealth(std::vector<HealthProbeTarget>{}, req, resp);
    EXPECT_EQ(resp.code, 503);
}

// --- CollectHealthProbeTargets ---

TEST(HealthTest, ProbeTargetsAreTheGlobalConnectionAndTheRequestsTenantPool) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HealthProbeTargets", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        WebApp& app = endpointHelper.GetWebApp();
        // A fresh registry, as on a server that has not served a request yet:
        // resolving the request's tenant must build its pool so it is probed.
        int built = 0;
        app.SetTenantResourceRegistry(std::make_shared<Tenancy::TenantResourceRegistry>(
            [&](const Tenancy::TenantContext& context) {
                ++built;
                auto resources = std::make_shared<Tenancy::TenantResources>();
                resources->context = context;
                resources->transactionProvider =
                    std::make_shared<AcceptingTransactionProvider>();
                return resources;
            }));

        crow::request req;
        std::vector<HealthProbeTarget> targets = CollectHealthProbeTargets(app, req);

        std::vector<std::string> names;
        for (const HealthProbeTarget& target : targets) {
            names.push_back(target.name);
        }
        EXPECT_EQ(names, (std::vector<std::string>{
            "global", testDb.GetDatabaseHelper().GetDatabaseName()}));
        EXPECT_EQ(built, 1);
    });

    Auth::ServerConfig::Shutdown();
}

TEST(HealthTest, WithoutTenancyOnlyTheGlobalConnectionIsProbed) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HealthNoTenancy", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        WebApp& app = endpointHelper.GetWebApp();
        app.SetTenantResourceRegistry(nullptr);

        crow::request req;
        std::vector<HealthProbeTarget> targets = CollectHealthProbeTargets(app, req);
        ASSERT_EQ(targets.size(), 1u);
        EXPECT_EQ(targets[0].name, "global");
    });

    Auth::ServerConfig::Shutdown();
}

// --- HTTP integration test (full route, via Crow) ---

TEST(HealthTest, HttpEndpointReturnsOkWhenDbWorks) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HealthHttpOk", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        crow::request req;
        req.method = crow::HTTPMethod::GET;
        req.url = "/api/health";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);
        EXPECT_EQ(resp.get_header_value("Content-Type"), "application/json");

        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["status"].Get<std::string>(), "ok");
        EXPECT_EQ(body["db"].Get<std::string>(), "ok");
        EXPECT_TRUE(body.HasChild("version", nullptr));
    });

    Auth::ServerConfig::Shutdown();
}

// The 10/15/2026 regression, end to end through the real route: the site's
// pooled connection is dead while the global connection works. Health used to
// probe only the global one and answer 200 throughout the outage.
TEST(HealthTest, HttpEndpointFailsWhenTheSitesPoolIsDeadThoughGlobalWorks) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HealthHttpDeadPool", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        endpointHelper.GetWebApp().SetTenantResourceRegistry(DeadPoolRegistry());

        crow::request req;
        req.method = crow::HTTPMethod::GET;
        req.url = "/api/health";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 503);
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["db"].Get<std::string>(), "fail");
        // Names the dead pool, and only that: the global connection was fine.
        EXPECT_EQ(FailedNames(body), std::vector<std::string>{
            testDb.GetDatabaseHelper().GetDatabaseName()});
    });

    Auth::ServerConfig::Shutdown();
}

TEST(HealthTest, HttpEndpointVersionFromEnv) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    VersionEnvScope scope;
    SetEnv("HONUWARE_VERSION", "v2026.04.27-test");

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HealthHttpVersion", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        crow::request req;
        req.method = crow::HTTPMethod::GET;
        req.url = "/api/health";
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["version"].Get<std::string>(), "v2026.04.27-test");
    });

    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
