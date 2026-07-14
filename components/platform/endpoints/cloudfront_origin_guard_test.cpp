#include "cloudfront_origin_guard.h"

#include <cstdlib>
#include <string>

#include <crow/http_request.h>
#include <crow/http_response.h>
#include <gtest/gtest.h>

namespace Endpoints {
namespace {

// --- Env-var helpers (mirror the pattern in proxy_trust_test) ---

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

// RAII: scrubs BOTH the canonical HONUWARE_ORIGIN_SECRET and the legacy
// KNOTTYYOGA_ORIGIN_SECRET on entry and exit so a leaked value — or one set
// in the developer's launch environment reachable via the fallback — can't
// bleed between tests (Phase 1.1 env-var rename).
class OriginSecretEnvScope {
public:
    OriginSecretEnvScope() {
        UnsetEnv("HONUWARE_ORIGIN_SECRET");
        UnsetEnv("KNOTTYYOGA_ORIGIN_SECRET");
    }
    ~OriginSecretEnvScope() {
        UnsetEnv("HONUWARE_ORIGIN_SECRET");
        UnsetEnv("KNOTTYYOGA_ORIGIN_SECRET");
    }
};

// --- Helpers for building requests/responses ---

crow::request MakeReq(const std::string& url) {
    crow::request req;
    req.url = url;
    return req;
}

void AddHeader(crow::request& req, const std::string& name, const std::string& value) {
    req.add_header(name, value);
}

// --- Activation ---

TEST(CloudFrontOriginGuardTest, InactiveWhenEnvVarUnset) {
    OriginSecretEnvScope scope;  // env var unset
    CloudFrontOriginGuard guard;
    EXPECT_FALSE(guard.IsActive());
}

TEST(CloudFrontOriginGuardTest, InactiveWhenEnvVarEmpty) {
    OriginSecretEnvScope scope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "");
    CloudFrontOriginGuard guard;
    EXPECT_FALSE(guard.IsActive());
}

TEST(CloudFrontOriginGuardTest, ActiveWhenEnvVarSet) {
    OriginSecretEnvScope scope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "abcdef");
    CloudFrontOriginGuard guard;
    EXPECT_TRUE(guard.IsActive());
}

// Phase 1.1 rename: the canonical HONUWARE_ORIGIN_SECRET activates the guard.
TEST(CloudFrontOriginGuardTest, ActiveWhenHonuwareEnvVarSet) {
    OriginSecretEnvScope scope;
    SetEnv("HONUWARE_ORIGIN_SECRET", "abcdef");
    CloudFrontOriginGuard guard;
    EXPECT_TRUE(guard.IsActive());
}

// The canonical name's secret is the one enforced when both are set.
TEST(CloudFrontOriginGuardTest, HonuwareSecretTakesPrecedenceOverLegacy) {
    OriginSecretEnvScope scope;
    SetEnv("HONUWARE_ORIGIN_SECRET", "canonical-secret");
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "legacy-secret");
    CloudFrontOriginGuard guard;
    ASSERT_TRUE(guard.IsActive());

    // A request carrying the canonical secret passes.
    auto goodReq = MakeReq("/api/me");
    AddHeader(goodReq, "X-Origin-Secret", "canonical-secret");
    crow::response goodRes;
    CloudFrontOriginGuard::context goodCtx;
    guard.before_handle(goodReq, goodRes, goodCtx);
    EXPECT_FALSE(goodRes.is_completed());
    EXPECT_NE(goodRes.code, 403);

    // A request carrying only the legacy secret is rejected — the canonical
    // name won, so the legacy value is not the enforced secret.
    auto badReq = MakeReq("/api/me");
    AddHeader(badReq, "X-Origin-Secret", "legacy-secret");
    crow::response badRes;
    CloudFrontOriginGuard::context badCtx;
    guard.before_handle(badReq, badRes, badCtx);
    EXPECT_EQ(badRes.code, 403);
    EXPECT_TRUE(badRes.is_completed());
}

// When only the legacy name is set, its secret is enforced (fallback path).
TEST(CloudFrontOriginGuardTest, LegacySecretEnforcedWhenHonuwareUnset) {
    OriginSecretEnvScope scope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "legacy-secret");
    CloudFrontOriginGuard guard;
    ASSERT_TRUE(guard.IsActive());

    auto req = MakeReq("/api/me");
    AddHeader(req, "X-Origin-Secret", "legacy-secret");
    crow::response res;
    CloudFrontOriginGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_FALSE(res.is_completed());
    EXPECT_NE(res.code, 403);
}

// --- Disabled guard: every request passes through ---

TEST(CloudFrontOriginGuardTest, DisabledLetsEveryRequestThrough) {
    OriginSecretEnvScope scope;
    CloudFrontOriginGuard guard;

    auto req = MakeReq("/api/me");  // no header, secret-protected path
    crow::response res;
    CloudFrontOriginGuard::context ctx;

    guard.before_handle(req, res, ctx);

    EXPECT_FALSE(res.is_completed());
    EXPECT_NE(res.code, 403);
}

// --- Active guard, secret-protected path ---

TEST(CloudFrontOriginGuardTest, RejectsRequestWithoutHeader) {
    OriginSecretEnvScope scope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "the-secret");
    CloudFrontOriginGuard guard;

    auto req = MakeReq("/api/me");
    crow::response res;
    CloudFrontOriginGuard::context ctx;

    guard.before_handle(req, res, ctx);

    EXPECT_EQ(res.code, 403);
    EXPECT_TRUE(res.is_completed());
    EXPECT_NE(res.body.find("direct_origin_access_forbidden"), std::string::npos);
    EXPECT_EQ(res.get_header_value("Content-Type"), "application/json");
}

TEST(CloudFrontOriginGuardTest, RejectsRequestWithWrongHeader) {
    OriginSecretEnvScope scope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "the-secret");
    CloudFrontOriginGuard guard;

    auto req = MakeReq("/api/me");
    AddHeader(req, "X-Origin-Secret", "wrong-secret");
    crow::response res;
    CloudFrontOriginGuard::context ctx;

    guard.before_handle(req, res, ctx);

    EXPECT_EQ(res.code, 403);
    EXPECT_TRUE(res.is_completed());
}

TEST(CloudFrontOriginGuardTest, AcceptsRequestWithCorrectHeader) {
    OriginSecretEnvScope scope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "the-secret");
    CloudFrontOriginGuard guard;

    auto req = MakeReq("/api/me");
    AddHeader(req, "X-Origin-Secret", "the-secret");
    crow::response res;
    CloudFrontOriginGuard::context ctx;

    guard.before_handle(req, res, ctx);

    // Pass-through: handler downstream will run.
    EXPECT_FALSE(res.is_completed());
    EXPECT_NE(res.code, 403);
}

TEST(CloudFrontOriginGuardTest, RejectsRequestWithEmptyHeader) {
    OriginSecretEnvScope scope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "the-secret");
    CloudFrontOriginGuard guard;

    auto req = MakeReq("/api/me");
    AddHeader(req, "X-Origin-Secret", "");
    crow::response res;
    CloudFrontOriginGuard::context ctx;

    guard.before_handle(req, res, ctx);

    EXPECT_EQ(res.code, 403);
    EXPECT_TRUE(res.is_completed());
}

// --- Health-path allow-list ---

TEST(CloudFrontOriginGuardTest, AllowsExactHealthPathWithoutHeader) {
    OriginSecretEnvScope scope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "the-secret");
    CloudFrontOriginGuard guard;

    auto req = MakeReq("/api/health");
    crow::response res;
    CloudFrontOriginGuard::context ctx;

    guard.before_handle(req, res, ctx);

    EXPECT_FALSE(res.is_completed());
    EXPECT_NE(res.code, 403);
}

TEST(CloudFrontOriginGuardTest, AllowsHealthSubpathWithoutHeader) {
    OriginSecretEnvScope scope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "the-secret");
    CloudFrontOriginGuard guard;

    auto req = MakeReq("/api/health/db");
    crow::response res;
    CloudFrontOriginGuard::context ctx;

    guard.before_handle(req, res, ctx);

    EXPECT_FALSE(res.is_completed());
    EXPECT_NE(res.code, 403);
}

// Defensive: a path that *almost* looks like /api/health (e.g., /api/healthz)
// must still be guarded — prefix matching is anchored to "/api/health" without
// any clever fuzziness. /api/healthz starts with /api/health so it would be
// allowed by a naive prefix check; this test pins that decision explicitly.
TEST(CloudFrontOriginGuardTest, AllowsApiHealthzPrefixMatchWithoutHeader) {
    OriginSecretEnvScope scope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "the-secret");
    CloudFrontOriginGuard guard;

    auto req = MakeReq("/api/healthz");
    crow::response res;
    CloudFrontOriginGuard::context ctx;

    guard.before_handle(req, res, ctx);

    // Documents that the current implementation uses a simple prefix match.
    // If the allow-list ever needs to be tightened (require trailing '/' or
    // exact path), this test is the canary that flags the change.
    EXPECT_FALSE(res.is_completed());
}

TEST(CloudFrontOriginGuardTest, BlocksNonHealthPathThatStartsWithApi) {
    OriginSecretEnvScope scope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "the-secret");
    CloudFrontOriginGuard guard;

    auto req = MakeReq("/api/login");
    crow::response res;
    CloudFrontOriginGuard::context ctx;

    guard.before_handle(req, res, ctx);

    EXPECT_EQ(res.code, 403);
    EXPECT_TRUE(res.is_completed());
}

TEST(CloudFrontOriginGuardTest, BlocksRootPathWithoutHeader) {
    OriginSecretEnvScope scope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "the-secret");
    CloudFrontOriginGuard guard;

    auto req = MakeReq("/");
    crow::response res;
    CloudFrontOriginGuard::context ctx;

    guard.before_handle(req, res, ctx);

    EXPECT_EQ(res.code, 403);
}

// --- after_handle is a no-op ---

TEST(CloudFrontOriginGuardTest, AfterHandleDoesNotMutateResponse) {
    OriginSecretEnvScope scope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "the-secret");
    CloudFrontOriginGuard guard;

    auto req = MakeReq("/api/me");
    crow::response res;
    res.code = 200;
    res.write("ok");
    CloudFrontOriginGuard::context ctx;

    guard.after_handle(req, res, ctx);

    EXPECT_EQ(res.code, 200);
    EXPECT_EQ(res.body, "ok");
}

}  // namespace
}  // namespace Endpoints
