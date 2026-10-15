#include "http_client.h"

#include <curl/curl.h>
#include <cstdlib>
#include <stdexcept>

namespace Http {

namespace {

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

size_t HeaderCallback(char* buffer, size_t size, size_t nitems,
                      HttpResponse* response) {
    size_t totalSize = size * nitems;
    ParseResponseHeaderLine(std::string_view(buffer, totalSize), *response);
    return totalSize;
}

bool IsSetCookie(std::string_view name) {
    constexpr std::string_view kSetCookie = "set-cookie";
    if (name.size() != kSetCookie.size()) return false;
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        if (lower != kSetCookie[i]) return false;
    }
    return true;
}

class HttpClientImpl : public HttpClient {
public:
    HttpClientImpl() = default;
    ~HttpClientImpl() override = default;

    HttpResponse Execute(const HttpRequest& request) override {
        CURL* curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }

        HttpResponse response;
        std::string responseBody;

        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);

        // Set method and body
        if (request.method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                             static_cast<long>(request.body.size()));
        } else if (request.method == "PUT") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                             static_cast<long>(request.body.size()));
        } else if (request.method == "DELETE") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        } else if (request.method == "PATCH") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                             static_cast<long>(request.body.size()));
        }
        // GET is default, no special handling needed

        // SSL certificate verification
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        const char* caBundle = std::getenv("CURL_CA_BUNDLE");
        if (caBundle) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, caBundle);
        } else {
            curl_easy_setopt(curl, CURLOPT_CAINFO, "certs/cacert.pem");
        }

        // Set headers
        struct curl_slist* headers = nullptr;
        for (const auto& [key, value] : request.headers) {
            std::string header = key + ": " + value;
            headers = curl_slist_append(headers, header.c_str());
        }
        if (headers) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        // Execute request
        CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            throw std::runtime_error(
                std::string("CURL error: ") + curl_easy_strerror(res));
        }

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        response.statusCode = static_cast<int>(httpCode);
        response.body = std::move(responseBody);
        return response;
    }
};

}  // namespace

void ParseResponseHeaderLine(std::string_view line, HttpResponse& response) {
    // Find the colon separator
    size_t colonPos = line.find(':');
    if (colonPos == std::string_view::npos) {
        return;
    }
    std::string key(line.substr(0, colonPos));
    std::string value(line.substr(colonPos + 1));

    // Trim whitespace from value
    size_t start = value.find_first_not_of(" \t");
    size_t end = value.find_last_not_of(" \t\r\n");
    if (start != std::string::npos && end != std::string::npos) {
        value = value.substr(start, end - start + 1);
    } else {
        value.clear();
    }

    if (IsSetCookie(key)) {
        response.setCookies.push_back(value);
    }
    response.headers[key] = value;
}

HttpClientPtr MakeHttpClient() {
    return std::make_shared<HttpClientImpl>();
}

}  // namespace Http
