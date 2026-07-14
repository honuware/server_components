#include "http_client_test_util.h"

#include <stdexcept>

namespace Http {
namespace Test {

HttpResponse TestHttpClient::Execute(const HttpRequest& request) {
    // Record the request for later verification
    requests_.push_back(request);

    // If a handler is set, use it
    if (responseHandler_) {
        return responseHandler_(request);
    }

    // Otherwise, use queued responses
    if (queuedResponses_.empty()) {
        throw std::runtime_error(
            "TestHttpClient: No response configured. "
            "Call SetResponseHandler, QueueResponse, or SetNextResponse before Execute.");
    }

    auto queued = std::move(queuedResponses_.front());
    queuedResponses_.pop();

    if (std::holds_alternative<QueuedError>(queued)) {
        throw std::runtime_error(std::get<QueuedError>(queued).message);
    }

    return std::get<HttpResponse>(queued);
}

void TestHttpClient::QueueResponse(HttpResponse response) {
    queuedResponses_.push(std::move(response));
}

void TestHttpClient::QueueResponse(int statusCode, const std::string& body) {
    HttpResponse response;
    response.statusCode = statusCode;
    response.body = body;
    queuedResponses_.push(std::move(response));
}

void TestHttpClient::QueueError(const std::string& errorMessage) {
    queuedResponses_.push(QueuedError{errorMessage});
}

void TestHttpClient::ClearQueuedResponses() {
    std::queue<std::variant<HttpResponse, QueuedError>> empty;
    queuedResponses_.swap(empty);
}

void TestHttpClient::SetNextResponse(HttpResponse response) {
    ClearQueuedResponses();
    QueueResponse(std::move(response));
}

void TestHttpClient::SetNextResponse(int statusCode, const std::string& body) {
    ClearQueuedResponses();
    QueueResponse(statusCode, body);
}

TestHttpClientPtr MakeTestHttpClient() {
    return std::make_shared<TestHttpClient>();
}

}  // namespace Test
}  // namespace Http
