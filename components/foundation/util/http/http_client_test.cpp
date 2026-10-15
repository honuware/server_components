#include "http_client.h"

#include <algorithm>
#include <cstdint>
#include <future>
#include <string>

#include <gtest/gtest.h>

#include <crow.h>

namespace Http {
namespace {

// Phase 13 — the ONLY test that executes the real libcurl-backed HttpClient.
//
// MakeHttpClient() had NO coverage at all: every other test in the suite runs
// against the TestHttpClient double (http_client_test_util.h), which is the
// right default — doubles are faster, deterministic, and need no network. But it
// left the actual production transport executed by nothing, while libcurl moved
// 7.86.0 -> 8.21.0 in this very migration and will keep moving. A transport that
// no test touches is a transport whose upgrades are verified by deployment.
//
// DELIBERATELY ONE TEST. This introduces a live-server pattern the codebase has
// otherwise avoided, and the justification is narrow: one uncovered production
// component. It does not generalise — a second live-server test needs its own
// argument, not a reference to this one. Everything else keeps using the double.
// (One has since made its own: platform/endpoints/api_client_end_to_end_test.cpp,
// Finishing website Phase 4.4 -- a seam that handle_full() cannot reach.)
//
// No ThreadPool and no database are involved, so the CLAUDE.md rule about calling
// ThreadPool::Shutdown() before a DB-touching assertion does not apply here.
// Nothing in this test re-enters the test transaction provider.
TEST(HttpClientTest, RealClientRoundTripsAgainstALiveServer) {
    crow::SimpleApp app;

    CROW_ROUTE(app, "/ping")
    ([] {
        crow::response response(200, "pong");
        response.set_header("X-Honuware-Probe", "yes");
        // Two Set-Cookie headers, as /api/login sends (session + csrft). The
        // header map keeps one; setCookies must keep both (Finishing website
        // Phase 4.4).
        response.add_header("Set-Cookie", "first=1; Path=/");
        response.add_header("Set-Cookie", "second=2; Path=/");
        return response;
    });

    // PORT 0, then read the OS-assigned port back.
    //
    // A hardcoded port is the classic way a test like this becomes flaky in
    // docker and CI — it passes locally and fails rarely, confusingly, and only
    // when something else happens to hold the port. crow::App::port() returns the
    // CONFIGURED value before the server starts and the acceptor's real
    // local_endpoint().port() afterwards, so this must be read AFTER
    // wait_for_server_start() or it reads back 0.
    //
    // Bound to 127.0.0.1 rather than all interfaces: the gate containers are on a
    // shared docker network, and a test server should not be reachable from it.
    std::future<void> serverDone =
        app.bindaddr("127.0.0.1").port(0).run_async();
    app.wait_for_server_start();

    const std::uint16_t port = app.port();
    ASSERT_NE(port, 0) << "server did not report a bound port";

    HttpResponse response;
    {
        HttpClientPtr client = MakeHttpClient();
        ASSERT_NE(client, nullptr);

        HttpRequest request;
        request.url = "http://127.0.0.1:" + std::to_string(port) + "/ping";
        request.method = "GET";
        request.headers["X-Honuware-Test"] = "phase13";

        response = client->Execute(request);
    }

    // Stop the server before asserting, so a failed expectation cannot leave the
    // io_context running and hang the suite.
    app.stop();
    serverDone.wait();

    EXPECT_EQ(response.statusCode, 200);
    EXPECT_EQ(response.body, "pong");

    // Response headers must survive the round trip too — the body alone would
    // still pass if header parsing were broken, and headers are what carry auth
    // and content type in real use.
    bool sawProbeHeader = false;
    for (const auto& [name, value] : response.headers) {
        std::string lowered;
        lowered.reserve(name.size());
        for (char c : name) {
            lowered.push_back(
                static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
        }
        if (lowered == "x-honuware-probe") {
            sawProbeHeader = true;
            EXPECT_EQ(value, "yes");
        }
    }
    EXPECT_TRUE(sawProbeHeader)
        << "response headers did not survive the round trip";

    // A repeated header reaches setCookies once per occurrence. Not in a fixed
    // order: Crow keeps response headers in an unordered_multimap, and
    // libstdc++ writes repeated names newest-first while MSVC writes them
    // oldest-first. (That is why the scheduler helper lost csrft in particular
    // on the Linux server: /api/login sets session_token then csrft, they went
    // out as csrft, session_token, and the last-wins header map kept
    // session_token.)
    ASSERT_EQ(response.setCookies.size(), 2u);
    EXPECT_NE(std::find(response.setCookies.begin(), response.setCookies.end(),
                        "first=1; Path=/"),
              response.setCookies.end());
    EXPECT_NE(std::find(response.setCookies.begin(), response.setCookies.end(),
                        "second=2; Path=/"),
              response.setCookies.end());
}

// ParseResponseHeaderLine is the per-line parser the libcurl client runs.

TEST(HttpClientTest, ParseResponseHeaderLineTrimsTheValue) {
    HttpResponse response;
    ParseResponseHeaderLine("Content-Type:   application/json \r\n", response);
    EXPECT_EQ(response.headers["Content-Type"], "application/json");
    EXPECT_TRUE(response.setCookies.empty());
}

TEST(HttpClientTest, ParseResponseHeaderLineIgnoresLinesWithoutAColon) {
    HttpResponse response;
    ParseResponseHeaderLine("HTTP/1.1 200 OK\r\n", response);
    ParseResponseHeaderLine("\r\n", response);
    EXPECT_TRUE(response.headers.empty());
    EXPECT_TRUE(response.setCookies.empty());
}

TEST(HttpClientTest, ParseResponseHeaderLineKeepsEverySetCookie) {
    HttpResponse response;
    ParseResponseHeaderLine("Set-Cookie: session=abc; HttpOnly\r\n", response);
    ParseResponseHeaderLine("set-cookie: csrft=xyz; Path=/\r\n", response);

    ASSERT_EQ(response.setCookies.size(), 2u);
    EXPECT_EQ(response.setCookies[0], "session=abc; HttpOnly");
    EXPECT_EQ(response.setCookies[1], "csrft=xyz; Path=/");
    // The header map still keeps one value per name, as before.
    EXPECT_EQ(response.headers["Set-Cookie"], "session=abc; HttpOnly");
    EXPECT_EQ(response.headers["set-cookie"], "csrft=xyz; Path=/");
}

TEST(HttpClientTest, ParseResponseHeaderLineRepeatedHeaderKeepsTheLastValue) {
    HttpResponse response;
    ParseResponseHeaderLine("Set-Cookie: a=1\r\n", response);
    ParseResponseHeaderLine("Set-Cookie: b=2\r\n", response);
    EXPECT_EQ(response.headers["Set-Cookie"], "b=2");
    EXPECT_EQ(response.setCookies.size(), 2u);
}

TEST(HttpClientTest, ParseResponseHeaderLineEmptyValue) {
    HttpResponse response;
    ParseResponseHeaderLine("X-Empty:\r\n", response);
    ASSERT_EQ(response.headers.count("X-Empty"), 1u);
    EXPECT_EQ(response.headers["X-Empty"], "");
}

// TLS is deliberately NOT covered here, and that is a decision rather than an
// oversight (Phase 13, final item).
//
// The real client always sets CURLOPT_SSL_VERIFYPEER/VERIFYHOST and resolves its
// CA bundle from CURL_CA_BUNDLE, falling back to the **working-directory-relative**
// path "certs/cacert.pem" (http_client.cpp:86-91). Testing that honestly would
// mean standing up an HTTPS server with a certificate this test trusts, which
// means generating or committing a test CA — a materially larger change than the
// one uncovered component this phase exists to close.
//
// What is worth knowing, because it is exactly the shape of thing that breaks in
// the release image rather than in the gate: that fallback path is CWD-relative,
// so the client only finds its CA bundle when the process happens to run from the
// directory containing certs/. build_and_test.sh cd's into the build directory
// before running the suite for this reason. A deployment that starts the server
// from anywhere else gets certificate failures that no test here would predict.

}  // namespace
}  // namespace Http
