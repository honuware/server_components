#include "api_client.h"

#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

#include "util/json_value.h"
#include "util/logging.h"

namespace Scheduler {

namespace {

// Trim leading/trailing ASCII whitespace.
std::string_view Trim(std::string_view s) {
    size_t start = 0;
    while (start < s.size() &&
           std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

// Case-insensitive string equality, ASCII only.
bool IEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

ApiClient::ApiClient(
    Http::HttpClientPtr httpClient,
    std::string baseUrl,
    std::vector<std::pair<std::string, std::string>> loginHeaders)
    : httpClient_(std::move(httpClient)),
      baseUrl_(std::move(baseUrl)),
      loginHeaders_(std::move(loginHeaders)) {
    // Strip a trailing slash so BuildUrl() doesn't emit "//path".
    while (!baseUrl_.empty() && baseUrl_.back() == '/') {
        baseUrl_.pop_back();
    }
}

bool ApiClient::Login(std::string_view email, std::string_view password) {
    email_ = std::string(email);
    password_ = std::string(password);

    Json::Value loginBody(Json::JsonObject{
        {"email", Json::Value(std::string(email))},
        {"password", Json::Value(std::string(password))},
        {"remember", Json::Value(false)}
    });

    Http::HttpRequest request;
    request.url = BuildUrl("/api/login");
    request.method = "POST";
    request.body = loginBody.ToString();
    request.headers["Content-Type"] = "application/json";
    // Deployment/tenant headers (e.g. X-Honuware-Site, X-Origin-Secret). Applied
    // after Content-Type so a caller could override it, mirroring ExecuteOnce.
    // Without the site header a control-mode server rejects /api/login before it
    // ever checks credentials, so these must ride on the login request itself.
    for (const auto& [name, value] : loginHeaders_) {
        request.headers[name] = value;
    }
    // Login itself doesn't need stored cookies, but if the server rotates
    // the session by issuing a new cookie on a subsequent login, we still
    // want to forget the old one first.
    cookies_.clear();

    Http::HttpResponse response = httpClient_->Execute(request);
    if (response.statusCode == 200) {
        HarvestSetCookies(response);
        LogInfo() << "[api_client] event=login_success email=" << email
                  << " status=" << response.statusCode
                  << " cookies=" << cookies_.size() << "\n";
        return true;
    }
    LogError() << "[api_client] event=login_failure email=" << email
               << " status=" << response.statusCode << "\n";
    return false;
}

Http::HttpResponse ApiClient::CallEndpoint(
    std::string_view method,
    std::string_view path,
    std::string_view body,
    const std::vector<std::pair<std::string, std::string>>& extraHeaders) {

    Http::HttpResponse response = ExecuteOnce(method, path, body, extraHeaders);

    // Retry once on 401, but only if we have credentials we can re-use.
    if (response.statusCode == 401 && !email_.empty() && !password_.empty()) {
        LogInfo() << "[api_client] event=reauth_required path=" << path
                  << " method=" << method << "\n";
        cookies_.clear();
        if (Login(email_, password_)) {
            response = ExecuteOnce(method, path, body, extraHeaders);
        } else {
            LogError() << "[api_client] event=reauth_failed path=" << path
                       << " method=" << method << "\n";
        }
    }

    return response;
}

Http::HttpResponse ApiClient::ExecuteOnce(
    std::string_view method,
    std::string_view path,
    std::string_view body,
    const std::vector<std::pair<std::string, std::string>>& extraHeaders) const {

    Http::HttpRequest request;
    request.url = BuildUrl(path);
    request.method = std::string(method);
    request.body = std::string(body);
    if (!body.empty()) {
        request.headers["Content-Type"] = "application/json";
    }
    std::string cookieHeader = BuildCookieHeader();
    if (!cookieHeader.empty()) {
        request.headers["Cookie"] = cookieHeader;
    }
    // Per-job headers are applied last so a job can override an engine-set
    // header (e.g. supply its own Content-Type) if it ever needs to.
    for (const auto& [name, value] : extraHeaders) {
        request.headers[name] = value;
    }
    return httpClient_->Execute(request);
}

bool ApiClient::HasCookie(std::string_view name) const {
    return cookies_.find(std::string(name)) != cookies_.end();
}

std::string ApiClient::GetCookieValue(std::string_view name) const {
    auto it = cookies_.find(std::string(name));
    if (it == cookies_.end()) return "";
    return it->second;
}

std::string ApiClient::BuildUrl(std::string_view path) const {
    if (path.empty() || path.front() != '/') {
        return baseUrl_ + "/" + std::string(path);
    }
    return baseUrl_ + std::string(path);
}

void ApiClient::StoreSetCookie(std::string_view setCookieValue) {
    // Crow emits "name=value; Path=/; HttpOnly; SameSite=None". We only
    // want the first segment.
    size_t semicolon = setCookieValue.find(';');
    std::string_view nameValue = (semicolon == std::string_view::npos)
        ? setCookieValue
        : setCookieValue.substr(0, semicolon);

    nameValue = Trim(nameValue);
    size_t eq = nameValue.find('=');
    if (eq == std::string_view::npos || eq == 0) {
        // No '=' or empty name — ignore malformed entries instead of throwing,
        // since the helper deals with one trusted origin and we'd rather
        // surface the resulting auth failure on the next request than crash.
        return;
    }
    std::string name(Trim(nameValue.substr(0, eq)));
    std::string value(Trim(nameValue.substr(eq + 1)));
    if (name.empty()) return;
    cookies_[name] = value;
}

std::string ApiClient::BuildCookieHeader() const {
    std::string out;
    for (const auto& [name, value] : cookies_) {
        if (!out.empty()) out += "; ";
        out += name;
        out += "=";
        out += value;
    }
    return out;
}

void ApiClient::HarvestSetCookies(const Http::HttpResponse& response) {
    for (const auto& [key, value] : response.headers) {
        if (IEquals(key, "Set-Cookie")) {
            StoreSetCookie(value);
        }
    }
}

}  // namespace Scheduler
