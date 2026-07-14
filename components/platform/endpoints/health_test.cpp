#include "health.h"

#include <cstdlib>
#include <memory>
#include <stdexcept>

#include <gtest/gtest.h>

#include "endpoints/endpoint_test_helper.h"
#include "web_app.h"
#include "business_logic/auth/server_config.h"
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

// RAII guard that restores KNOTTYYOGA_VERSION to its prior value on scope exit.
// Tests can mutate the env var freely without affecting siblings.
class VersionEnvScope {
public:
    VersionEnvScope() {
        const char* val = std::getenv("KNOTTYYOGA_VERSION");
        had_ = (val != nullptr);
        if (had_) prior_.assign(val);
        UnsetEnv("KNOTTYYOGA_VERSION");
    }
    ~VersionEnvScope() {
        if (had_) SetEnv("KNOTTYYOGA_VERSION", prior_.c_str());
        else UnsetEnv("KNOTTYYOGA_VERSION");
    }
private:
    bool had_ = false;
    std::string prior_;
};

// Test-only TransactionProvider that always throws. Used to drive the
// DB-failure path through GetHealth without having to take down a real DB.
class ThrowingTransactionProvider : public TransactionProvider {
public:
    void RunInTransaction(DatabaseFunc) override {
        throw std::runtime_error("simulated DB failure");
    }
};

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

// --- GetBuildVersion: env var handling ---

TEST(HealthTest, GetBuildVersionFromEnv) {
    VersionEnvScope scope;
    SetEnv("KNOTTYYOGA_VERSION", "v2026.04.27-abcdef");
    EXPECT_EQ(GetBuildVersion(), "v2026.04.27-abcdef");
}

TEST(HealthTest, GetBuildVersionUnsetReturnsUnknown) {
    VersionEnvScope scope;
    EXPECT_EQ(GetBuildVersion(), "unknown");
}

TEST(HealthTest, GetBuildVersionEmptyReturnsUnknown) {
    VersionEnvScope scope;
    SetEnv("KNOTTYYOGA_VERSION", "");
    EXPECT_EQ(GetBuildVersion(), "unknown");
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

TEST(HealthTest, HttpEndpointVersionFromEnv) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    VersionEnvScope scope;
    SetEnv("KNOTTYYOGA_VERSION", "v2026.04.27-test");

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
