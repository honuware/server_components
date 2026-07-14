#include "error_response.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "util/error_codes.h"
#include "util/json_value.h"

namespace {

// Helper to parse response body as JSON
Json::Value ParseResponseBody(const crow::response& resp) {
    return Json::Value::FromText(resp.body);
}

TEST(ErrorResponseTest, CreateBasicError) {
    crow::response resp = ErrorResponse::Create(
        "test_error",
        "Test Error",
        400,
        "This is a test error");

    EXPECT_EQ(resp.code, 400);

    Json::Value body = ParseResponseBody(resp);
    EXPECT_TRUE(body.HasChildren());
    EXPECT_EQ(body["type"].Get<std::string>(), "test_error");
    EXPECT_EQ(body["title"].Get<std::string>(), "Test Error");
    EXPECT_EQ(body["status"].Get<int64_t>(), 400);
    EXPECT_EQ(body["detail"].Get<std::string>(), "This is a test error");
}

TEST(ErrorResponseTest, CreateWithExtensions) {
    Json::JsonObject extensions;
    extensions["field"] = Json::Value("email");
    extensions["custom_key"] = Json::Value("custom_value");

    crow::response resp = ErrorResponse::Create(
        "test_error",
        "Test Error",
        400,
        "Test with extensions",
        extensions);

    Json::Value body = ParseResponseBody(resp);
    EXPECT_EQ(body["field"].Get<std::string>(), "email");
    EXPECT_EQ(body["custom_key"].Get<std::string>(), "custom_value");
}

TEST(ErrorResponseTest, ValidationErrorBasic) {
    crow::response resp = ErrorResponse::ValidationError("Email is required");

    EXPECT_EQ(resp.code, 400);

    Json::Value body = ParseResponseBody(resp);
    EXPECT_EQ(body["type"].Get<std::string>(), "validation_error");
    EXPECT_EQ(body["title"].Get<std::string>(), "Validation Error");
    EXPECT_EQ(body["status"].Get<int64_t>(), 400);
    EXPECT_EQ(body["detail"].Get<std::string>(), "Email is required");
}

TEST(ErrorResponseTest, ValidationErrorWithField) {
    crow::response resp = ErrorResponse::ValidationError(
        "Email is required", "email", "required");

    Json::Value body = ParseResponseBody(resp);
    EXPECT_EQ(body["field"].Get<std::string>(), "email");
    EXPECT_EQ(body["constraint"].Get<std::string>(), "required");
}

TEST(ErrorResponseTest, BadRequest) {
    crow::response resp = ErrorResponse::BadRequest("Invalid JSON syntax");

    EXPECT_EQ(resp.code, 400);

    Json::Value body = ParseResponseBody(resp);
    EXPECT_EQ(body["type"].Get<std::string>(), "bad_request");
    EXPECT_EQ(body["title"].Get<std::string>(), "Bad Request");
    EXPECT_EQ(body["detail"].Get<std::string>(), "Invalid JSON syntax");
}

TEST(ErrorResponseTest, NotAuthenticated) {
    crow::response resp = ErrorResponse::NotAuthenticated();

    EXPECT_EQ(resp.code, 401);

    Json::Value body = ParseResponseBody(resp);
    EXPECT_EQ(body["type"].Get<std::string>(), "not_authenticated");
    EXPECT_EQ(body["title"].Get<std::string>(), "Authentication Required");
}

TEST(ErrorResponseTest, InvalidCredentials) {
    crow::response resp = ErrorResponse::InvalidCredentials();

    EXPECT_EQ(resp.code, 401);

    Json::Value body = ParseResponseBody(resp);
    EXPECT_EQ(body["type"].Get<std::string>(), "invalid_credentials");
    EXPECT_EQ(body["title"].Get<std::string>(), "Invalid Credentials");
    EXPECT_EQ(body["detail"].Get<std::string>(),
        "The email or password you entered is incorrect");
}

TEST(ErrorResponseTest, SessionExpired) {
    crow::response resp = ErrorResponse::SessionExpired();

    EXPECT_EQ(resp.code, 401);

    Json::Value body = ParseResponseBody(resp);
    EXPECT_EQ(body["type"].Get<std::string>(), "session_expired");
    EXPECT_EQ(body["title"].Get<std::string>(), "Session Expired");
}

TEST(ErrorResponseTest, NotAuthorized) {
    crow::response resp = ErrorResponse::NotAuthorized();

    EXPECT_EQ(resp.code, 403);

    Json::Value body = ParseResponseBody(resp);
    EXPECT_EQ(body["type"].Get<std::string>(), "not_authorized");
    EXPECT_EQ(body["title"].Get<std::string>(), "Not Authorized");
}

TEST(ErrorResponseTest, NotFound) {
    crow::response resp = ErrorResponse::NotFound("User not found");

    EXPECT_EQ(resp.code, 404);

    Json::Value body = ParseResponseBody(resp);
    EXPECT_EQ(body["type"].Get<std::string>(), "not_found");
    EXPECT_EQ(body["title"].Get<std::string>(), "Not Found");
    EXPECT_EQ(body["detail"].Get<std::string>(), "User not found");
}

TEST(ErrorResponseTest, IdempotencyConflict) {
    crow::response resp = ErrorResponse::IdempotencyConflict();

    EXPECT_EQ(resp.code, 409);

    Json::Value body = ParseResponseBody(resp);
    EXPECT_EQ(body["type"].Get<std::string>(), "idempotency_conflict");
    EXPECT_EQ(body["title"].Get<std::string>(), "Conflict");
}

TEST(ErrorResponseTest, InternalError) {
    crow::response resp = ErrorResponse::InternalError();

    EXPECT_EQ(resp.code, 500);

    Json::Value body = ParseResponseBody(resp);
    EXPECT_EQ(body["type"].Get<std::string>(), "internal_error");
    EXPECT_EQ(body["title"].Get<std::string>(), "Internal Server Error");
}

// Phase 7.3 of the security review: the response body NEVER echoes
// the caller's argument — even when caller passes raw exception text
// from a catch block (the most common case). Pin this contract so a
// future "convenience" refactor that surfaces the context to the
// client breaks the build.
TEST(ErrorResponseTest, InternalErrorDoesNotEchoServerContext) {
    const std::string sensitiveDetail =
        "ERROR: column \"password_hash\" does not exist (LINE 1: ...)";
    crow::response resp = ErrorResponse::InternalError(sensitiveDetail);

    EXPECT_EQ(resp.code, 500);
    Json::Value body = ParseResponseBody(resp);

    // Type/title/status are unchanged.
    EXPECT_EQ(body["type"].Get<std::string>(), "internal_error");
    EXPECT_EQ(body["title"].Get<std::string>(), "Internal Server Error");
    EXPECT_EQ(body["status"].Get<int64_t>(), 500);

    // Detail is the GENERIC string — does not include any of the
    // sensitive content from the input.
    std::string detail = body["detail"].Get<std::string>();
    EXPECT_EQ(detail, "An unexpected error occurred. Please try again later");
    EXPECT_EQ(detail.find("password_hash"), std::string::npos)
        << "InternalError must not surface column names from server-side errors";
    EXPECT_EQ(detail.find("ERROR:"), std::string::npos);
    EXPECT_EQ(detail.find("LINE 1"), std::string::npos);

    // Full body string also doesn't contain it (defensive — the
    // structured detail check above already covers this, but this
    // catches a future change that adds an extension field).
    EXPECT_EQ(resp.body.find("password_hash"), std::string::npos);
    EXPECT_EQ(resp.body.find("LINE 1"), std::string::npos);
}

// Empty server context still produces the generic body. Locks the
// "always generic" rule so callers don't have to guard against
// passing empty strings.
TEST(ErrorResponseTest, InternalErrorEmptyServerContextStillGeneric) {
    crow::response resp = ErrorResponse::InternalError("");

    Json::Value body = ParseResponseBody(resp);
    EXPECT_EQ(body["detail"].Get<std::string>(),
              "An unexpected error occurred. Please try again later");
}

// Default-arg overload (no argument) produces the same body as
// passing an empty string — exception-free convenience for
// catch-all blocks that don't have an exception in hand.
TEST(ErrorResponseTest, InternalErrorDefaultArgIsGeneric) {
    crow::response respDefault = ErrorResponse::InternalError();
    crow::response respExplicit = ErrorResponse::InternalError("");

    Json::Value bodyDefault = ParseResponseBody(respDefault);
    Json::Value bodyExplicit = ParseResponseBody(respExplicit);
    EXPECT_EQ(bodyDefault["detail"].Get<std::string>(),
              bodyExplicit["detail"].Get<std::string>());
}

TEST(ErrorResponseTest, PaymentDeclined) {
    crow::response resp = ErrorResponse::PaymentDeclined(
        "Your card was declined",
        "square",
        "CARD_DECLINED");

    EXPECT_EQ(resp.code, 402);

    Json::Value body = ParseResponseBody(resp);
    EXPECT_EQ(body["type"].Get<std::string>(), "payment_declined");
    EXPECT_EQ(body["title"].Get<std::string>(), "Payment Declined");
    EXPECT_EQ(body["provider"].Get<std::string>(), "square");
    EXPECT_EQ(body["provider_code"].Get<std::string>(), "CARD_DECLINED");
}

TEST(ErrorResponseTest, PaymentFailed) {
    crow::response resp = ErrorResponse::PaymentFailed(
        "Payment processing failed",
        "square",
        "PROCESSING_ERROR");

    EXPECT_EQ(resp.code, 402);

    Json::Value body = ParseResponseBody(resp);
    EXPECT_EQ(body["type"].Get<std::string>(), "payment_failed");
    EXPECT_EQ(body["title"].Get<std::string>(), "Payment Failed");
    EXPECT_EQ(body["provider"].Get<std::string>(), "square");
    EXPECT_EQ(body["provider_code"].Get<std::string>(), "PROCESSING_ERROR");
}

TEST(ErrorResponseTest, ContentTypeIsJson) {
    crow::response resp = ErrorResponse::BadRequest("test");

    // Check that Content-Type header is set
    auto it = resp.headers.find("Content-Type");
    ASSERT_NE(it, resp.headers.end());
    EXPECT_EQ(it->second, "application/json");
}

TEST(ErrorResponseTest, BuildJsonBodyReturnsValidJson) {
    Json::Value body = ErrorResponse::BuildJsonBody(
        "test_error",
        "Test Error",
        400,
        "Test detail");

    EXPECT_TRUE(body.HasChildren());
    EXPECT_EQ(body["type"].Get<std::string>(), "test_error");
    EXPECT_EQ(body["title"].Get<std::string>(), "Test Error");
    EXPECT_EQ(body["status"].Get<int64_t>(), 400);
    EXPECT_EQ(body["detail"].Get<std::string>(), "Test detail");
}

// Test error codes helpers
TEST(ErrorCodesTest, GetTitleForType) {
    EXPECT_EQ(ErrorCodes::GetTitleForType(ErrorCodes::kValidationError),
        "Validation Error");
    EXPECT_EQ(ErrorCodes::GetTitleForType(ErrorCodes::kNotAuthenticated),
        "Authentication Required");
    EXPECT_EQ(ErrorCodes::GetTitleForType(ErrorCodes::kInternalError),
        "Internal Server Error");
    EXPECT_EQ(ErrorCodes::GetTitleForType("unknown_type"), "Error");
}

TEST(ErrorCodesTest, GetStatusForType) {
    EXPECT_EQ(ErrorCodes::GetStatusForType(ErrorCodes::kValidationError), 400);
    EXPECT_EQ(ErrorCodes::GetStatusForType(ErrorCodes::kNotAuthenticated), 401);
    EXPECT_EQ(ErrorCodes::GetStatusForType(ErrorCodes::kNotAuthorized), 403);
    EXPECT_EQ(ErrorCodes::GetStatusForType(ErrorCodes::kNotFound), 404);
    EXPECT_EQ(ErrorCodes::GetStatusForType(ErrorCodes::kInternalError), 500);
    EXPECT_EQ(ErrorCodes::GetStatusForType("unknown_type"), 500);
}

} // namespace
