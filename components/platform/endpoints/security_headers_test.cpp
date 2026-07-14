#include "security_headers.h"

#include <crow/http_request.h>
#include <crow/http_response.h>
#include <gtest/gtest.h>

#include "business_logic/auth/server_config.h"
#include "endpoints/endpoint_test_helper.h"
#include "test/src/util/database_test_helper.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"

// Phase 7.1 of the security review.
//
// Two flavors of testing:
//   - Direct: instantiate SecurityHeaders, call after_handle on a
//     synthesized response, inspect headers. Same shape as
//     cloudfront_origin_guard_test / csrf_guard_test. This is the
//     workhorse — it covers the enable/disable toggle, the always-on
//     header set, and the prod-only branch.
//   - The integration test runs through the full middleware chain
//     via EndpointTestHelper::handle_full to confirm the after_handle
//     ordering puts headers on actual responses (covered separately
//     by security_headers_integration_test below).
//
// ServerConfig is a singleton; tests Shutdown() on enter and exit
// so siblings can't see leaked state from prior runs.

namespace Endpoints {
namespace {

// Convenience: extract a single header from a crow::response. Crow
// stores headers in a multi-map; for our middleware each emitted
// header is set exactly once via set_header (which replaces, not
// appends), so we just read the first match. Note: Crow's
// `get_header_value` is non-const, so the helpers take a mutable
// reference.
std::string GetHeader(crow::response& res, const std::string& name) {
    return std::string(res.get_header_value(name));
}

bool HasHeader(crow::response& res, const std::string& name) {
    return !res.get_header_value(name).empty();
}

// ---- Toggle / default ----

TEST(SecurityHeadersTest, EnabledByDefault) {
    SecurityHeaders mw;
    EXPECT_TRUE(mw.IsEnabled());
}

TEST(SecurityHeadersTest, SetEnabledFlipsState) {
    SecurityHeaders mw;
    mw.SetEnabled(false);
    EXPECT_FALSE(mw.IsEnabled());
    mw.SetEnabled(true);
    EXPECT_TRUE(mw.IsEnabled());
}

TEST(SecurityHeadersTest, DisabledMiddlewareIsANoOp) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    SecurityHeaders mw;
    mw.SetEnabled(false);
    crow::request req;
    crow::response res;
    SecurityHeaders::context ctx;
    mw.after_handle(req, res, ctx);

    EXPECT_FALSE(HasHeader(res, "X-Content-Type-Options"));
    EXPECT_FALSE(HasHeader(res, "Referrer-Policy"));
    EXPECT_FALSE(HasHeader(res, "Permissions-Policy"));
    EXPECT_FALSE(HasHeader(res, "Server"));
    EXPECT_FALSE(HasHeader(res, "Strict-Transport-Security"));
    EXPECT_FALSE(HasHeader(res, "Content-Security-Policy"));

    Auth::ServerConfig::Shutdown();
}

// ---- Always-on headers (test mode) ----

TEST(SecurityHeadersTest, TestModeEmitsAlwaysOnHeaders) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    SecurityHeaders mw;
    crow::request req;
    crow::response res;
    SecurityHeaders::context ctx;
    mw.after_handle(req, res, ctx);

    EXPECT_EQ(GetHeader(res, "X-Content-Type-Options"), "nosniff");
    EXPECT_EQ(GetHeader(res, "Referrer-Policy"),
              "strict-origin-when-cross-origin");
    EXPECT_EQ(GetHeader(res, "Permissions-Policy"),
              "camera=(), microphone=(), geolocation=()");
    // Phase 3.2: the Server banner is brand-specific, so the framework emits
    // no Server header unless the app opts in via SetServerBanner(...).
    EXPECT_FALSE(HasHeader(res, "Server"));

    Auth::ServerConfig::Shutdown();
}

// ---- Server banner (app opt-in) ----

TEST(SecurityHeadersTest, NoServerBannerByDefault) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    SecurityHeaders mw;
    EXPECT_TRUE(mw.GetServerBanner().empty());
    crow::request req;
    crow::response res;
    SecurityHeaders::context ctx;
    mw.after_handle(req, res, ctx);

    EXPECT_FALSE(HasHeader(res, "Server"));

    Auth::ServerConfig::Shutdown();
}

TEST(SecurityHeadersTest, SetServerBannerEmitsServerHeader) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    SecurityHeaders mw;
    mw.SetServerBanner("Knotty Yoga");
    EXPECT_EQ(mw.GetServerBanner(), "Knotty Yoga");
    crow::request req;
    crow::response res;
    SecurityHeaders::context ctx;
    mw.after_handle(req, res, ctx);

    EXPECT_EQ(GetHeader(res, "Server"), "Knotty Yoga");

    Auth::ServerConfig::Shutdown();
}

TEST(SecurityHeadersTest, EmitsVaryOrigin) {
    // Phase 7.2: Crow's CORSHandler does NOT add Vary: Origin
    // itself, so SecurityHeaders does. Pin the contract — without
    // this, shared caches would happily serve a CORS response keyed
    // for one Origin to a request from a different Origin.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    SecurityHeaders mw;
    crow::request req;
    crow::response res;
    SecurityHeaders::context ctx;
    mw.after_handle(req, res, ctx);

    EXPECT_EQ(GetHeader(res, "Vary"), "Origin");

    Auth::ServerConfig::Shutdown();
}

// ---- Prod-only headers ----

TEST(SecurityHeadersTest, TestModeOmitsHstsAndCsp) {
    // HSTS over plain HTTP locks dev browsers into HTTPS-only for a
    // full year — a footgun with no prod benefit. CSP is dev-noisy.
    // Test mode must NOT emit either.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    SecurityHeaders mw;
    crow::request req;
    crow::response res;
    SecurityHeaders::context ctx;
    mw.after_handle(req, res, ctx);

    EXPECT_FALSE(HasHeader(res, "Strict-Transport-Security"));
    EXPECT_FALSE(HasHeader(res, "Content-Security-Policy"));

    Auth::ServerConfig::Shutdown();
}

TEST(SecurityHeadersTest, NoServerConfigOmitsHstsAndCsp) {
    // Defensive: if ServerConfig isn't initialized at all (some
    // early-startup paths), the middleware should fall back to
    // "not prod" rather than throw. We still emit the always-on
    // headers; only the prod-gated ones are skipped.
    Auth::ServerConfig::Shutdown();

    SecurityHeaders mw;
    crow::request req;
    crow::response res;
    SecurityHeaders::context ctx;
    mw.after_handle(req, res, ctx);

    EXPECT_FALSE(HasHeader(res, "Strict-Transport-Security"));
    EXPECT_FALSE(HasHeader(res, "Content-Security-Policy"));
    // Always-on still fires.
    EXPECT_EQ(GetHeader(res, "X-Content-Type-Options"), "nosniff");
}

TEST(SecurityHeadersTest, ProdModeEmitsHstsAndCsp) {
    // Initialize ServerConfig in prod mode via the standard
    // EndpointTestHelper path — same shape session_test.cpp uses
    // for InitializeFromLoginProdModeCookieHasSecureAndDomain.
    Auth::ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ProdModeEmitsHstsAndCsp",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        secrets->AddSecret(tx, Secrets::kWebsiteAddress, "knottyyoga.example");
        Auth::ServerConfig::Initialize(
            tx, secrets, &endpointHelper.GetWebApp());
        ASSERT_TRUE(Auth::ServerConfig::GetInstance().IsProdMode());

        SecurityHeaders mw;
        // The banner is app-injected (main.cpp sets it); set it here so the
        // "Server fires alongside the prod headers" check below still holds.
        mw.SetServerBanner("Knotty Yoga");
        crow::request req;
        crow::response res;
        SecurityHeaders::context ctx;
        mw.after_handle(req, res, ctx);

        std::string hsts = GetHeader(res, "Strict-Transport-Security");
        EXPECT_FALSE(hsts.empty()) << "HSTS must fire in prod";
        EXPECT_NE(hsts.find("max-age=31536000"), std::string::npos);
        EXPECT_NE(hsts.find("includeSubDomains"), std::string::npos);
        EXPECT_NE(hsts.find("preload"), std::string::npos);

        std::string csp = GetHeader(res, "Content-Security-Policy");
        EXPECT_FALSE(csp.empty()) << "CSP must fire in prod";
        // Spot-check the load-bearing directives.
        EXPECT_NE(csp.find("default-src 'self'"), std::string::npos);
        EXPECT_NE(csp.find("frame-ancestors 'none'"), std::string::npos);
        EXPECT_NE(csp.find("base-uri 'none'"), std::string::npos);

        // Always-on fires too — not exclusive with prod headers.
        EXPECT_EQ(GetHeader(res, "X-Content-Type-Options"), "nosniff");
        EXPECT_EQ(GetHeader(res, "Server"), "Knotty Yoga");
    });

    Auth::ServerConfig::Shutdown();
}

// ---- Integration via the WebApp's middleware instance ----
//
// Crow's `handle_full` is a debug shortcut that does NOT run
// middleware after_handle (see crow/app.h: "primarily used for
// debugging"). To verify the middleware actually fires through the
// App-owned instance (as opposed to a stack-local one), we invoke
// after_handle on the instance the WebApp registered. Same code
// path Crow runs on a real connection — minus the routing layer.

TEST(SecurityHeadersTest, IntegrationHeadersLandOnAppOwnedInstance) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IntegrationHeadersLandOnAppOwnedInstance",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        // EndpointTestHelper turns the middleware off by default
        // (per Phase 7.1 test-integration). Re-enable on the
        // App-owned instance so we exercise the real wiring.
        auto& mw = endpointHelper.GetWebApp().GetApp()
            .get_middleware<SecurityHeaders>();
        mw.SetEnabled(true);
        // Mirror main.cpp's app-side wiring: the brand banner is opt-in.
        mw.SetServerBanner("Knotty Yoga");

        crow::request req;
        req.method = crow::HTTPMethod::Get;
        req.url = "/api/health";
        crow::response resp;
        SecurityHeaders::context ctx;
        mw.after_handle(req, resp, ctx);

        EXPECT_EQ(GetHeader(resp, "X-Content-Type-Options"), "nosniff");
        EXPECT_EQ(GetHeader(resp, "Referrer-Policy"),
                  "strict-origin-when-cross-origin");
        EXPECT_EQ(GetHeader(resp, "Server"), "Knotty Yoga");
        EXPECT_EQ(GetHeader(resp, "Vary"), "Origin");
        // Test mode → no HSTS / CSP.
        EXPECT_FALSE(HasHeader(resp, "Strict-Transport-Security"));
        EXPECT_FALSE(HasHeader(resp, "Content-Security-Policy"));
    });

    Auth::ServerConfig::Shutdown();
}

// Regression: the App-owned instance starts disabled (per the
// EndpointTestHelper defaults) so existing endpoint tests that
// don't re-enable it never see the new headers.
TEST(SecurityHeadersTest, EndpointTestHelperLeavesMiddlewareDisabled) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EndpointTestHelperLeavesMiddlewareDisabled",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto& mw = endpointHelper.GetWebApp().GetApp()
            .get_middleware<SecurityHeaders>();
        EXPECT_FALSE(mw.IsEnabled())
            << "EndpointTestHelper must leave SecurityHeaders disabled "
               "so existing endpoint tests don't see the new headers "
               "in their responses";

        // Belt-and-suspenders: invoking after_handle on the disabled
        // instance is a no-op.
        crow::request req;
        crow::response resp;
        SecurityHeaders::context ctx;
        mw.after_handle(req, resp, ctx);
        EXPECT_FALSE(HasHeader(resp, "X-Content-Type-Options"));
        EXPECT_FALSE(HasHeader(resp, "Referrer-Policy"));
    });

    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
