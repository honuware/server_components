#pragma once

#include <map>
#include <memory>
#include <string>

namespace Http {

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::map<std::string, std::string> headers;
};

struct HttpRequest {
    std::string url;
    std::string method;  // "GET", "POST", etc.
    std::string body;
    std::map<std::string, std::string> headers;
};

class HttpClient {
public:
    virtual ~HttpClient() = default;

    virtual HttpResponse Execute(const HttpRequest& request) = 0;

protected:
    HttpClient() = default;
    HttpClient(const HttpClient&) = default;
    HttpClient& operator=(const HttpClient&) = default;
};

using HttpClientPtr = std::shared_ptr<HttpClient>;

// Factory for production implementation (uses libcurl)
HttpClientPtr MakeHttpClient();

}  // namespace Http
