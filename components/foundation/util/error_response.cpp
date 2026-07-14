#include "util/error_response.h"

#include <crow.h>
#include "util/error_codes.h"

namespace ErrorResponse {

namespace {

crow::response BuildResponse(int status, const Json::Value& body) {
    crow::response resp(status);
    resp.set_header("Content-Type", "application/json");
    resp.write(body.ToString());
    return resp;
}

} // namespace

Json::Value BuildJsonBody(
    std::string_view type,
    std::string_view title,
    int status,
    std::string_view detail,
    const Json::JsonObject& extensions) {

    Json::JsonObject body;
    body["type"] = Json::Value(std::string(type));
    body["title"] = Json::Value(std::string(title));
    body["status"] = Json::Value(static_cast<int64_t>(status));
    body["detail"] = Json::Value(std::string(detail));

    // Add any extension fields
    for (const auto& [key, value] : extensions) {
        body[key] = value;
    }

    return Json::Value(std::move(body));
}

crow::response Create(
    std::string_view type,
    std::string_view title,
    int status,
    std::string_view detail,
    const Json::JsonObject& extensions) {

    CROW_LOG_ERROR << "ErrorResponse " << status << " " << title << ": " << detail;
    Json::Value body = BuildJsonBody(type, title, status, detail, extensions);
    return BuildResponse(status, body);
}

crow::response ValidationError(
    std::string_view detail,
    std::string_view field,
    std::string_view constraint) {

    Json::JsonObject extensions;
    if (!field.empty()) {
        extensions["field"] = Json::Value(std::string(field));
    }
    if (!constraint.empty()) {
        extensions["constraint"] = Json::Value(std::string(constraint));
    }

    return Create(
        ErrorCodes::kValidationError,
        ErrorCodes::GetTitleForType(ErrorCodes::kValidationError),
        ErrorCodes::GetStatusForType(ErrorCodes::kValidationError),
        detail,
        extensions);
}

crow::response BadRequest(std::string_view detail) {
    return Create(
        ErrorCodes::kBadRequest,
        ErrorCodes::GetTitleForType(ErrorCodes::kBadRequest),
        ErrorCodes::GetStatusForType(ErrorCodes::kBadRequest),
        detail);
}

crow::response NotAuthenticated(std::string_view detail) {
    return Create(
        ErrorCodes::kNotAuthenticated,
        ErrorCodes::GetTitleForType(ErrorCodes::kNotAuthenticated),
        ErrorCodes::GetStatusForType(ErrorCodes::kNotAuthenticated),
        detail);
}

crow::response InvalidCredentials(std::string_view detail) {
    return Create(
        ErrorCodes::kInvalidCredentials,
        ErrorCodes::GetTitleForType(ErrorCodes::kInvalidCredentials),
        ErrorCodes::GetStatusForType(ErrorCodes::kInvalidCredentials),
        detail);
}

crow::response SessionExpired(std::string_view detail) {
    return Create(
        ErrorCodes::kSessionExpired,
        ErrorCodes::GetTitleForType(ErrorCodes::kSessionExpired),
        ErrorCodes::GetStatusForType(ErrorCodes::kSessionExpired),
        detail);
}

crow::response NotAuthorized(std::string_view detail) {
    return Create(
        ErrorCodes::kNotAuthorized,
        ErrorCodes::GetTitleForType(ErrorCodes::kNotAuthorized),
        ErrorCodes::GetStatusForType(ErrorCodes::kNotAuthorized),
        detail);
}

crow::response NotFound(std::string_view detail) {
    return Create(
        ErrorCodes::kNotFound,
        ErrorCodes::GetTitleForType(ErrorCodes::kNotFound),
        ErrorCodes::GetStatusForType(ErrorCodes::kNotFound),
        detail);
}

crow::response IdempotencyConflict(std::string_view detail) {
    return Create(
        ErrorCodes::kIdempotencyConflict,
        ErrorCodes::GetTitleForType(ErrorCodes::kIdempotencyConflict),
        ErrorCodes::GetStatusForType(ErrorCodes::kIdempotencyConflict),
        detail);
}

crow::response TooManyAttempts(std::string_view detail) {
    return Create(
        ErrorCodes::kTooManyAttempts,
        ErrorCodes::GetTitleForType(ErrorCodes::kTooManyAttempts),
        ErrorCodes::GetStatusForType(ErrorCodes::kTooManyAttempts),
        detail);
}

crow::response InternalError(std::string_view serverContext) {
    // Phase 7.3 of the security review: the response body is ALWAYS
    // the generic message — never echoes the caller's argument. The
    // argument is logged server-side so operators can correlate
    // the response with the underlying error in the journal.
    static constexpr std::string_view kGenericDetail =
        "An unexpected error occurred. Please try again later";
    if (!serverContext.empty()) {
        // Surface the server context in a dedicated log line. The
        // generic Create() call below also logs at LogError level,
        // so this is an additional context line — operators can
        // grep `event=internal_error_context` to see what blew up
        // without exposing it to clients.
        CROW_LOG_ERROR << "[error_response] event=internal_error_context "
                       << "context=\"" << serverContext << "\"";
    }
    return Create(
        ErrorCodes::kInternalError,
        ErrorCodes::GetTitleForType(ErrorCodes::kInternalError),
        ErrorCodes::GetStatusForType(ErrorCodes::kInternalError),
        kGenericDetail);
}

crow::response PaymentDeclined(
    std::string_view detail,
    std::string_view provider,
    std::string_view providerCode) {

    Json::JsonObject extensions;
    if (!provider.empty()) {
        extensions["provider"] = Json::Value(std::string(provider));
    }
    if (!providerCode.empty()) {
        extensions["provider_code"] = Json::Value(std::string(providerCode));
    }

    return Create(
        ErrorCodes::kPaymentDeclined,
        ErrorCodes::GetTitleForType(ErrorCodes::kPaymentDeclined),
        ErrorCodes::GetStatusForType(ErrorCodes::kPaymentDeclined),
        detail,
        extensions);
}

crow::response PaymentFailed(
    std::string_view detail,
    std::string_view provider,
    std::string_view providerCode) {

    Json::JsonObject extensions;
    if (!provider.empty()) {
        extensions["provider"] = Json::Value(std::string(provider));
    }
    if (!providerCode.empty()) {
        extensions["provider_code"] = Json::Value(std::string(providerCode));
    }

    return Create(
        ErrorCodes::kPaymentFailed,
        ErrorCodes::GetTitleForType(ErrorCodes::kPaymentFailed),
        ErrorCodes::GetStatusForType(ErrorCodes::kPaymentFailed),
        detail,
        extensions);
}

} // namespace ErrorResponse
