#include "proxy_trust.h"

#include <cstdlib>
#include <string>

#include <crow.h>
#include <gtest/gtest.h>

namespace Auth {
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

// RAII guard that scrubs BOTH the canonical HONUWARE_TRUST_PROXY and the
// legacy KNOTTYYOGA_TRUST_PROXY on entry and exit so a leaked env var from
// one test — or from the developer's launch environment via the fallback —
// cannot influence the next (Phase 1.1 env-var rename).
class ProxyTrustEnvScope {
public:
    ProxyTrustEnvScope() {
        UnsetEnv("HONUWARE_TRUST_PROXY");
        UnsetEnv("KNOTTYYOGA_TRUST_PROXY");
    }
    ~ProxyTrustEnvScope() {
        UnsetEnv("HONUWARE_TRUST_PROXY");
        UnsetEnv("KNOTTYYOGA_TRUST_PROXY");
    }
};

crow::request MakeReq() {
    return crow::request{};
}

void AddHeader(crow::request& req, std::string_view name, std::string_view value) {
    req.add_header(std::string(name), std::string(value));
}

// --- ProxyTrustEnabled ---

TEST(ProxyTrustEnabledTest, DefaultsToFalseWhenUnset) {
    ProxyTrustEnvScope scope;
    EXPECT_FALSE(ProxyTrustEnabled());
}

TEST(ProxyTrustEnabledTest, EmptyValueIsFalse) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "");
    EXPECT_FALSE(ProxyTrustEnabled());
}

TEST(ProxyTrustEnabledTest, OneIsTrue) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    EXPECT_TRUE(ProxyTrustEnabled());
}

// Phase 1.1 rename: the canonical HONUWARE_TRUST_PROXY name works on its own.
TEST(ProxyTrustEnabledTest, HonuwareNameIsTrue) {
    ProxyTrustEnvScope scope;
    SetEnv("HONUWARE_TRUST_PROXY", "1");
    EXPECT_TRUE(ProxyTrustEnabled());
}

// The canonical name takes precedence over the legacy one: HONUWARE="0"
// (even set-empty-to-non-empty "0") wins over KNOTTYYOGA="1".
TEST(ProxyTrustEnabledTest, HonuwareNameWinsOverLegacy) {
    ProxyTrustEnvScope scope;
    SetEnv("HONUWARE_TRUST_PROXY", "0");
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    EXPECT_FALSE(ProxyTrustEnabled());
}

// When the canonical name is unset, the legacy name still works (fallback).
TEST(ProxyTrustEnabledTest, LegacyNameUsedWhenHonuwareUnset) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "true");
    EXPECT_TRUE(ProxyTrustEnabled());
}

TEST(ProxyTrustEnabledTest, TrueLowercaseIsTrue) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "true");
    EXPECT_TRUE(ProxyTrustEnabled());
}

TEST(ProxyTrustEnabledTest, TrueMixedCaseIsTrue) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "True");
    EXPECT_TRUE(ProxyTrustEnabled());
}

TEST(ProxyTrustEnabledTest, ZeroIsFalse) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "0");
    EXPECT_FALSE(ProxyTrustEnabled());
}

TEST(ProxyTrustEnabledTest, FalseIsFalse) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "false");
    EXPECT_FALSE(ProxyTrustEnabled());
}

TEST(ProxyTrustEnabledTest, GarbageValueIsFalse) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "yes-please");
    EXPECT_FALSE(ProxyTrustEnabled());
}

// --- ResolveViewerScheme ---

TEST(ResolveViewerSchemeTest, ReturnsEmptyWhenProxyNotTrusted) {
    ProxyTrustEnvScope scope;  // KNOTTYYOGA_TRUST_PROXY unset
    auto req = MakeReq();
    AddHeader(req, "X-Forwarded-Proto", "https");
    EXPECT_EQ(ResolveViewerScheme(req), "");
}

TEST(ResolveViewerSchemeTest, ReturnsHeaderWhenTrusted) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    auto req = MakeReq();
    AddHeader(req, "X-Forwarded-Proto", "https");
    EXPECT_EQ(ResolveViewerScheme(req), "https");
}

TEST(ResolveViewerSchemeTest, ReturnsHttpWhenTrustedAndHeaderIsHttp) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    auto req = MakeReq();
    AddHeader(req, "X-Forwarded-Proto", "http");
    EXPECT_EQ(ResolveViewerScheme(req), "http");
}

TEST(ResolveViewerSchemeTest, ReturnsEmptyWhenTrustedButHeaderMissing) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    auto req = MakeReq();
    EXPECT_EQ(ResolveViewerScheme(req), "");
}

TEST(ResolveViewerSchemeTest, TrimsLeadingAndTrailingWhitespace) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    auto req = MakeReq();
    AddHeader(req, "X-Forwarded-Proto", "  https  ");
    EXPECT_EQ(ResolveViewerScheme(req), "https");
}

// --- ResolveViewerIp ---

TEST(ResolveViewerIpTest, ReturnsEmptyWhenProxyNotTrusted) {
    ProxyTrustEnvScope scope;
    auto req = MakeReq();
    AddHeader(req, "X-Forwarded-For", "203.0.113.7");
    EXPECT_EQ(ResolveViewerIp(req), "");
}

TEST(ResolveViewerIpTest, ReturnsSingleIpWhenTrusted) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    auto req = MakeReq();
    AddHeader(req, "X-Forwarded-For", "203.0.113.7");
    EXPECT_EQ(ResolveViewerIp(req), "203.0.113.7");
}

TEST(ResolveViewerIpTest, ReturnsFirstIpFromCommaSeparatedList) {
    // Per RFC 7239 (and CloudFront's docs), X-Forwarded-For is
    // "client, proxy1, proxy2, ..." — only the first entry is the
    // original viewer. The rest is the proxy chain and would be wrong
    // to attribute to the viewer.
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    auto req = MakeReq();
    AddHeader(req, "X-Forwarded-For",
        "203.0.113.7, 198.51.100.42, 192.0.2.1");
    EXPECT_EQ(ResolveViewerIp(req), "203.0.113.7");
}

TEST(ResolveViewerIpTest, TrimsWhitespaceAroundFirstIp) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    auto req = MakeReq();
    AddHeader(req, "X-Forwarded-For", "  203.0.113.7  ");
    EXPECT_EQ(ResolveViewerIp(req), "203.0.113.7");
}

TEST(ResolveViewerIpTest, ReturnsEmptyWhenTrustedButHeaderMissing) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    auto req = MakeReq();
    EXPECT_EQ(ResolveViewerIp(req), "");
}

TEST(ResolveViewerIpTest, ReturnsEmptyWhenTrustedAndHeaderEmpty) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    auto req = MakeReq();
    AddHeader(req, "X-Forwarded-For", "");
    EXPECT_EQ(ResolveViewerIp(req), "");
}

// --- ResolveClientIp (Phase 5.5 of the security review) ---

// When the proxy is trusted and the header is set, ResolveClientIp
// matches ResolveViewerIp.
TEST(ResolveClientIpTest, PrefersForwardedHeaderWhenProxyTrusted) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    auto req = MakeReq();
    AddHeader(req, "X-Forwarded-For", "203.0.113.7, 198.51.100.42");
    req.remote_ip_address = "172.16.0.5";  // CloudFront edge — should be ignored
    EXPECT_EQ(ResolveClientIp(req), "203.0.113.7");
}

// Without a trusted proxy, the connection peer address is the only
// signal we have. Tests that synthesize a request without a peer get
// the empty string — the gate treats that as "skip per-IP path".
TEST(ResolveClientIpTest, FallsBackToConnectionPeerWhenProxyUntrusted) {
    ProxyTrustEnvScope scope;  // env var unset
    auto req = MakeReq();
    AddHeader(req, "X-Forwarded-For", "203.0.113.7");  // ignored without trust
    req.remote_ip_address = "127.0.0.1";
    EXPECT_EQ(ResolveClientIp(req), "127.0.0.1");
}

// Proxy is trusted but the header is missing — the connection peer
// is still the right fallback (the request didn't actually transit
// through the trusted proxy).
TEST(ResolveClientIpTest, FallsBackToConnectionPeerWhenForwardedHeaderMissing) {
    ProxyTrustEnvScope scope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    auto req = MakeReq();
    req.remote_ip_address = "10.0.0.1";
    EXPECT_EQ(ResolveClientIp(req), "10.0.0.1");
}

// Neither source has a value — return empty so the gate's per-IP
// check skips. Locks the "no IP available" contract.
TEST(ResolveClientIpTest, ReturnsEmptyWhenNeitherSourceAvailable) {
    ProxyTrustEnvScope scope;  // env var unset
    auto req = MakeReq();
    EXPECT_EQ(ResolveClientIp(req), "");
}

}  // namespace
}  // namespace Auth
