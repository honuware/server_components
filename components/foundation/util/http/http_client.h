#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Http {

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    // One value per header name: a header the server sends twice keeps only
    // its LAST value here.
    std::map<std::string, std::string> headers;
    // Every Set-Cookie header, in the order received. Set-Cookie is the one
    // header a server routinely repeats -- /api/login sends the session cookie
    // AND the csrft cookie -- so it cannot rely on `headers` alone. That map
    // kept only the last one, so the scheduler helper never had the csrft
    // cookie, and every scheduled job was refused by CsrfGuard from 10/8/2026.
    std::vector<std::string> setCookies;
};

// Parses one raw response-header line ("Name: value\r\n") into `response`:
// into `headers`, and also onto `setCookies` when the name is Set-Cookie (in
// any case). Lines without a colon -- the status line, the blank line that
// ends the headers -- are ignored. The libcurl client calls this per line;
// exposed so the parsing can be tested without a network.
void ParseResponseHeaderLine(std::string_view line, HttpResponse& response);

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
