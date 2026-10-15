#include <gtest/gtest.h>
#include "endpoint_auth_helper.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>

#include "endpoints/endpoint_test_helper.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/tenancy/tenant_resources.h"
#include "sql_util/database_access/transaction_provider.h"
#include "test/src/util/database_test_helper.h"

namespace {

TEST(EndpointAuthHelperTest, InitializeBasic) {
    // Placeholder test - intentionally empty for now.
}

// RAII stdout capture: LogError() writes to std::cout.
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

class DeadTransactionProvider : public TransactionProvider {
public:
    void RunInTransaction(DatabaseFunc) override {
        throw std::runtime_error("Lost connection to the database server");
    }
};

size_t CountOccurrences(const std::string& text, const std::string& needle) {
    size_t count = 0;
    for (size_t pos = text.find(needle); pos != std::string::npos;
         pos = text.find(needle, pos + needle.size())) {
        ++count;
    }
    return count;
}

// Finishing website Phase 4.3. Initialize() swallows a failure while restoring
// the session, so the request carries on signed-out. On 10/15/2026 a dead
// database connection failed EVERY request there and nothing reached the log.
// Now it is logged -- once a minute, not once per request.
TEST(EndpointAuthHelperTest, SwallowedSetupFailureIsLoggedOnceAMinute) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetupFailureLogged", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        WebApp& app = endpointHelper.GetWebApp();
        // The site's pool is dead, as on 10/15.
        app.SetTenantResourceRegistry(std::make_shared<Tenancy::TenantResourceRegistry>(
            [](const Tenancy::TenantContext& context) {
                auto resources = std::make_shared<Tenancy::TenantResources>();
                resources->context = context;
                resources->transactionProvider =
                    std::make_shared<DeadTransactionProvider>();
                return resources;
            }));

        crow::request req;
        req.url = "/api/classes";
        std::string captured;
        {
            CoutCapture capture;
            for (int request = 0; request < 3; ++request) {
                crow::response resp;
                EndpointAuthHelper helper(app, req, resp);
                EXPECT_NO_THROW(helper.Initialize());
                // The request still carries on, signed out.
                EXPECT_FALSE(helper.GetSession().IsLoggedIn());
            }
            captured = capture.str();
        }

        EXPECT_EQ(CountOccurrences(captured, "[endpoint_auth] event=request_setup_failed"), 1u)
            << "captured: " << captured;
        EXPECT_NE(captured.find("Lost connection to the database server"), std::string::npos);
        EXPECT_NE(captured.find("path=/api/classes"), std::string::npos);
    });

    Auth::ServerConfig::Shutdown();
}

// A healthy request writes nothing.
TEST(EndpointAuthHelperTest, ASuccessfulSetupLogsNothing) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetupSucceedsQuietly", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        crow::request req;
        crow::response resp;
        std::string captured;
        {
            CoutCapture capture;
            EndpointAuthHelper helper(endpointHelper.GetWebApp(), req, resp);
            helper.Initialize();
            captured = capture.str();
        }
        EXPECT_EQ(captured.find("request_setup_failed"), std::string::npos)
            << "captured: " << captured;
    });

    Auth::ServerConfig::Shutdown();
}

} // namespace
