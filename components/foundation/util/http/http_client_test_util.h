#pragma once

#include "http_client.h"

#include <functional>
#include <queue>
#include <variant>
#include <vector>

namespace Http {
namespace Test {

using RequestList = std::vector<HttpRequest>;

class TestHttpClient : public HttpClient {
public:
    TestHttpClient() = default;
    TestHttpClient(const TestHttpClient&) = delete;
    TestHttpClient& operator=(const TestHttpClient&) = delete;
    ~TestHttpClient() override = default;

    HttpResponse Execute(const HttpRequest& request) override;

    // Test verification helpers
    const RequestList& GetRequests() const { return requests_; }
    void ClearRequests() { requests_.clear(); }
    size_t GetRequestCount() const { return requests_.size(); }

    // Set up fake responses - handler takes precedence over queued responses
    using ResponseHandler = std::function<HttpResponse(const HttpRequest&)>;
    void SetResponseHandler(ResponseHandler handler) { responseHandler_ = handler; }
    void ClearResponseHandler() { responseHandler_ = nullptr; }

    // Queue responses for sequential consumption
    void QueueResponse(HttpResponse response);
    void QueueResponse(int statusCode, const std::string& body);
    void QueueError(const std::string& errorMessage);
    void ClearQueuedResponses();

    // Simple helpers for single response scenarios
    void SetNextResponse(HttpResponse response);
    void SetNextResponse(int statusCode, const std::string& body);

private:
    RequestList requests_;
    ResponseHandler responseHandler_;

    struct QueuedError {
        std::string message;
    };
    std::queue<std::variant<HttpResponse, QueuedError>> queuedResponses_;
};

using TestHttpClientPtr = std::shared_ptr<TestHttpClient>;

TestHttpClientPtr MakeTestHttpClient();

}  // namespace Test
}  // namespace Http
