#include "csrf_guard.h"

#include <string>

#include <crow/http_request.h>
#include <crow/http_response.h>
#include <crow/common.h>
#include <gtest/gtest.h>

// Phase 4.2 of the security review.
//
// The CsrfGuard middleware is unit-testable in isolation: construct
// one, hand it a synthesized crow::request and crow::response, and
// observe whether before_handle calls res.end(). This is the same
// shape cloudfront_origin_guard_test.cpp uses.
//
// Coverage:
//   - Enabled by default; SetEnabled toggles the gate.
//   - GET / HEAD / OPTIONS bypass.
//   - Bootstrap paths (/api/login, /api/register, /api/remember,
//     /api/verify) bypass — including with a query string.
//   - Missing csrft cookie → 403 with the right JSON body.
//   - Missing X-CSRF-Token header → 403.
//   - Mismatched cookie / header → 403.
//   - Matching cookie / header → pass.
//   - Whitespace-tolerant cookie parsing.

namespace Endpoints {
namespace {

crow::request MakeReq(crow::HTTPMethod method, const std::string& url) {
    crow::request req;
    req.method = method;
    req.url = url;
    return req;
}

void AddHeader(crow::request& req, const std::string& name,
               const std::string& value) {
    req.add_header(name, value);
}

// Returns true when before_handle ended the response (i.e. the
// guard rejected). Crow's response::is_completed() reflects the
// res.end() call.
bool WasRejected(const crow::response& res) {
    return res.is_completed();
}

// ---------- Toggle / default state ----------

TEST(CsrfGuardTest, EnabledByDefault) {
    CsrfGuard guard;
    EXPECT_TRUE(guard.IsEnabled());
}

TEST(CsrfGuardTest, SetEnabledTogglesActivation) {
    CsrfGuard guard;
    guard.SetEnabled(false);
    EXPECT_FALSE(guard.IsEnabled());
    guard.SetEnabled(true);
    EXPECT_TRUE(guard.IsEnabled());
}

TEST(CsrfGuardTest, DisabledLetsEveryRequestThrough) {
    CsrfGuard guard;
    guard.SetEnabled(false);

    auto req = MakeReq(crow::HTTPMethod::Post, "/api/some_state_change");
    // No cookies, no header — would normally be rejected.
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);

    EXPECT_FALSE(WasRejected(res));
}

// ---------- Method-based bypass ----------

TEST(CsrfGuardTest, GetRequestBypasses) {
    CsrfGuard guard;  // enabled
    auto req = MakeReq(crow::HTTPMethod::Get, "/api/me");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_FALSE(WasRejected(res));
}

TEST(CsrfGuardTest, HeadRequestBypasses) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Head, "/api/me");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_FALSE(WasRejected(res));
}

TEST(CsrfGuardTest, OptionsRequestBypasses) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Options, "/api/me");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_FALSE(WasRejected(res));
}

// ---------- Bootstrap path bypass ----------

TEST(CsrfGuardTest, LoginPathBypasses) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/login");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_FALSE(WasRejected(res));
}

TEST(CsrfGuardTest, RegisterPathBypasses) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/register");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_FALSE(WasRejected(res));
}

TEST(CsrfGuardTest, RememberPathBypasses) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/remember");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_FALSE(WasRejected(res));
}

TEST(CsrfGuardTest, VerifyPathBypasses) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/verify");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_FALSE(WasRejected(res));
}

TEST(CsrfGuardTest, BootstrapPathWithQueryStringStillBypasses) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/login?next=/account");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_FALSE(WasRejected(res));
}

// Sanity: a path that *contains* a bootstrap name as a substring is
// NOT exempt. Otherwise an attacker could craft `/api/login_evil`
// and slip through.
TEST(CsrfGuardTest, BootstrapPrefixIsExactMatchOnly) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/login_evil");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_TRUE(WasRejected(res))
        << "/api/login_evil must NOT be treated as a bootstrap path";
}

// ---------- Token enforcement ----------

TEST(CsrfGuardTest, MissingCookieAndHeaderRejected) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/some_state_change");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);

    EXPECT_TRUE(WasRejected(res));
    EXPECT_EQ(res.code, 403);
    EXPECT_EQ(res.get_header_value("Content-Type"), "application/json");
    EXPECT_NE(res.body.find("csrf_token_missing_or_invalid"), std::string::npos);
}

TEST(CsrfGuardTest, MissingHeaderRejected) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/some_state_change");
    AddHeader(req, "Cookie", "csrft=abcdef");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);

    EXPECT_TRUE(WasRejected(res));
    EXPECT_EQ(res.code, 403);
}

TEST(CsrfGuardTest, MissingCookieRejected) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/some_state_change");
    AddHeader(req, "X-CSRF-Token", "abcdef");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);

    EXPECT_TRUE(WasRejected(res));
    EXPECT_EQ(res.code, 403);
}

TEST(CsrfGuardTest, MismatchedCookieAndHeaderRejected) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/some_state_change");
    AddHeader(req, "Cookie", "csrft=cookie-value");
    AddHeader(req, "X-CSRF-Token", "header-value");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);

    EXPECT_TRUE(WasRejected(res));
    EXPECT_EQ(res.code, 403);
}

TEST(CsrfGuardTest, MatchingCookieAndHeaderAllowed) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/some_state_change");
    AddHeader(req, "Cookie", "csrft=match-this-token");
    AddHeader(req, "X-CSRF-Token", "match-this-token");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);

    EXPECT_FALSE(WasRejected(res));
}

// PUT/PATCH/DELETE go through the same gate as POST.
TEST(CsrfGuardTest, PutWithoutTokenRejected) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Put, "/api/some_state_change");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_TRUE(WasRejected(res));
    EXPECT_EQ(res.code, 403);
}

TEST(CsrfGuardTest, PatchWithoutTokenRejected) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Patch, "/api/some_state_change");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_TRUE(WasRejected(res));
    EXPECT_EQ(res.code, 403);
}

TEST(CsrfGuardTest, DeleteWithoutTokenRejected) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Delete, "/api/some_state_change");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_TRUE(WasRejected(res));
    EXPECT_EQ(res.code, 403);
}

// ---------- Cookie-header parsing edge cases ----------

// The Cookie header concatenates name=value pairs with `; `. The csrft
// cookie may sit anywhere in that header. Walk through the realistic
// cases to lock the parser against drift.
TEST(CsrfGuardTest, CookieHeaderWithMultiplePairs) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/some_state_change");
    AddHeader(req, "Cookie",
        "session_token=abc; csrft=match-this-token; device_token=def");
    AddHeader(req, "X-CSRF-Token", "match-this-token");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_FALSE(WasRejected(res));
}

TEST(CsrfGuardTest, CookieHeaderWithoutCsrftPair) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/some_state_change");
    AddHeader(req, "Cookie", "session_token=abc; device_token=def");
    AddHeader(req, "X-CSRF-Token", "any-value");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_TRUE(WasRejected(res))
        << "csrft missing from Cookie header must be a rejection";
}

TEST(CsrfGuardTest, CookieHeaderWithCsrftAtEndNoTrailingSemicolon) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/some_state_change");
    AddHeader(req, "Cookie", "session_token=abc; csrft=tail-token");
    AddHeader(req, "X-CSRF-Token", "tail-token");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_FALSE(WasRejected(res));
}

// Empty cookie value (`csrft=`) must be treated as absent — never as
// a successful match against an empty header. ConstantTimeEqual on
// two empty strings would technically be true, so the guard's
// "csrft.empty() OR presented.empty()" short-circuit is the
// load-bearing line. Cover it explicitly.
TEST(CsrfGuardTest, EmptyCsrftCookieValueRejectedEvenWithEmptyHeader) {
    CsrfGuard guard;
    auto req = MakeReq(crow::HTTPMethod::Post, "/api/some_state_change");
    AddHeader(req, "Cookie", "csrft=");
    AddHeader(req, "X-CSRF-Token", "");
    crow::response res;
    CsrfGuard::context ctx;
    guard.before_handle(req, res, ctx);
    EXPECT_TRUE(WasRejected(res))
        << "Empty csrft cookie + empty header is not a valid match";
}

}  // namespace
}  // namespace Endpoints
