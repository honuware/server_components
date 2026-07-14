#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "http_client_test_util.h"

namespace {

using ::testing::HasSubstr;

TEST(TestHttpClientTest, RecordsRequests) {
    auto testClient = Http::Test::MakeTestHttpClient();
    testClient->SetNextResponse(200, "OK");

    Http::HttpRequest request;
    request.url = "https://example.com/api";
    request.method = "POST";
    request.body = R"({"key": "value"})";
    request.headers = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer token123"}
    };

    testClient->Execute(request);

    ASSERT_EQ(testClient->GetRequestCount(), 1);
    const auto& recorded = testClient->GetRequests()[0];
    EXPECT_EQ(recorded.url, "https://example.com/api");
    EXPECT_EQ(recorded.method, "POST");
    EXPECT_EQ(recorded.body, R"({"key": "value"})");
    EXPECT_EQ(recorded.headers.at("Content-Type"), "application/json");
    EXPECT_EQ(recorded.headers.at("Authorization"), "Bearer token123");
}

TEST(TestHttpClientTest, ReturnsQueuedResponses) {
    auto testClient = Http::Test::MakeTestHttpClient();

    testClient->QueueResponse(200, "First response");
    testClient->QueueResponse(201, "Second response");
    testClient->QueueResponse(404, "Not found");

    Http::HttpRequest request;
    request.url = "https://example.com";
    request.method = "GET";

    auto r1 = testClient->Execute(request);
    EXPECT_EQ(r1.statusCode, 200);
    EXPECT_EQ(r1.body, "First response");

    auto r2 = testClient->Execute(request);
    EXPECT_EQ(r2.statusCode, 201);
    EXPECT_EQ(r2.body, "Second response");

    auto r3 = testClient->Execute(request);
    EXPECT_EQ(r3.statusCode, 404);
    EXPECT_EQ(r3.body, "Not found");
}

TEST(TestHttpClientTest, QueueResponseWithFullObject) {
    auto testClient = Http::Test::MakeTestHttpClient();

    Http::HttpResponse response;
    response.statusCode = 202;
    response.body = "Accepted";
    response.headers = {{"X-Custom-Header", "custom-value"}};

    testClient->QueueResponse(response);

    Http::HttpRequest request;
    request.url = "https://example.com";
    request.method = "GET";

    auto r = testClient->Execute(request);
    EXPECT_EQ(r.statusCode, 202);
    EXPECT_EQ(r.body, "Accepted");
    EXPECT_EQ(r.headers.at("X-Custom-Header"), "custom-value");
}

TEST(TestHttpClientTest, SetNextResponseClearsQueue) {
    auto testClient = Http::Test::MakeTestHttpClient();

    testClient->QueueResponse(200, "Queued");
    testClient->SetNextResponse(201, "Replaced");

    Http::HttpRequest request;
    request.url = "https://example.com";
    request.method = "GET";

    auto r = testClient->Execute(request);
    EXPECT_EQ(r.statusCode, 201);
    EXPECT_EQ(r.body, "Replaced");

    // Second call should fail - queue was cleared and only one response added
    EXPECT_THROW({
        testClient->Execute(request);
    }, std::runtime_error);
}

TEST(TestHttpClientTest, QueueErrorThrowsOnExecute) {
    auto testClient = Http::Test::MakeTestHttpClient();
    testClient->QueueError("Connection refused");

    Http::HttpRequest request;
    request.url = "https://example.com";
    request.method = "GET";

    try {
        testClient->Execute(request);
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        EXPECT_THAT(e.what(), HasSubstr("Connection refused"));
    }
}

TEST(TestHttpClientTest, MixedResponsesAndErrors) {
    auto testClient = Http::Test::MakeTestHttpClient();

    testClient->QueueResponse(200, "OK");
    testClient->QueueError("Network timeout");
    testClient->QueueResponse(201, "Created");

    Http::HttpRequest request;
    request.url = "https://example.com";
    request.method = "GET";

    auto r1 = testClient->Execute(request);
    EXPECT_EQ(r1.statusCode, 200);

    EXPECT_THROW({
        testClient->Execute(request);
    }, std::runtime_error);

    auto r3 = testClient->Execute(request);
    EXPECT_EQ(r3.statusCode, 201);
}

TEST(TestHttpClientTest, ResponseHandlerTakesPrecedence) {
    auto testClient = Http::Test::MakeTestHttpClient();

    testClient->QueueResponse(200, "Queued - should not be used");

    testClient->SetResponseHandler([](const Http::HttpRequest& req) {
        Http::HttpResponse response;
        response.statusCode = 418;
        response.body = "I'm a teapot: " + req.url;
        return response;
    });

    Http::HttpRequest request;
    request.url = "https://example.com/tea";
    request.method = "GET";

    auto r = testClient->Execute(request);
    EXPECT_EQ(r.statusCode, 418);
    EXPECT_THAT(r.body, HasSubstr("teapot"));
    EXPECT_THAT(r.body, HasSubstr("https://example.com/tea"));
}

TEST(TestHttpClientTest, ClearResponseHandlerFallsBackToQueue) {
    auto testClient = Http::Test::MakeTestHttpClient();

    testClient->SetResponseHandler([](const Http::HttpRequest&) {
        Http::HttpResponse response;
        response.statusCode = 418;
        return response;
    });

    testClient->QueueResponse(200, "From queue");

    Http::HttpRequest request;
    request.url = "https://example.com";
    request.method = "GET";

    auto r1 = testClient->Execute(request);
    EXPECT_EQ(r1.statusCode, 418);  // From handler

    testClient->ClearResponseHandler();

    auto r2 = testClient->Execute(request);
    EXPECT_EQ(r2.statusCode, 200);  // From queue
}

TEST(TestHttpClientTest, ClearRequestsResetsHistory) {
    auto testClient = Http::Test::MakeTestHttpClient();
    testClient->QueueResponse(200, "OK");
    testClient->QueueResponse(200, "OK");

    Http::HttpRequest request;
    request.url = "https://example.com";
    request.method = "GET";

    testClient->Execute(request);
    EXPECT_EQ(testClient->GetRequestCount(), 1);

    testClient->ClearRequests();
    EXPECT_EQ(testClient->GetRequestCount(), 0);

    testClient->Execute(request);
    EXPECT_EQ(testClient->GetRequestCount(), 1);
}

TEST(TestHttpClientTest, ClearQueuedResponsesEmptiesQueue) {
    auto testClient = Http::Test::MakeTestHttpClient();

    testClient->QueueResponse(200, "OK");
    testClient->QueueResponse(201, "Created");
    testClient->ClearQueuedResponses();

    Http::HttpRequest request;
    request.url = "https://example.com";
    request.method = "GET";

    // Should throw because queue is empty
    EXPECT_THROW({
        testClient->Execute(request);
    }, std::runtime_error);
}

TEST(TestHttpClientTest, ThrowsWhenNoResponseConfigured) {
    auto testClient = Http::Test::MakeTestHttpClient();

    Http::HttpRequest request;
    request.url = "https://example.com";
    request.method = "GET";

    try {
        testClient->Execute(request);
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        EXPECT_THAT(e.what(), HasSubstr("No response configured"));
    }
}

}  // namespace
