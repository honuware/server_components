#pragma once

#include <string>
#include <string_view>
#include <crow/http_response.h>

#include "util/json_value.h"

namespace ErrorResponse {

// Create a generic RFC 7807 error response with optional extension fields
crow::response Create(
    std::string_view type,
    std::string_view title,
    int status,
    std::string_view detail,
    const Json::JsonObject& extensions = {});

// Factory methods for common errors

// 400 Bad Request - Validation error with optional field and constraint info
crow::response ValidationError(
    std::string_view detail,
    std::string_view field = "",
    std::string_view constraint = "");

// 400 Bad Request - General bad request (malformed request body, invalid JSON)
crow::response BadRequest(std::string_view detail = "The request was malformed or invalid");

// 401 Unauthorized - Authentication required (no valid session)
crow::response NotAuthenticated(std::string_view detail = "Authentication is required to access this resource");

// 401 Unauthorized - Invalid credentials (wrong email/password)
crow::response InvalidCredentials(std::string_view detail = "The email or password you entered is incorrect");

// 401 Unauthorized - Session expired
crow::response SessionExpired(std::string_view detail = "Your session has expired. Please log in again");

// 403 Forbidden - Not authorized (logged in but lacks permission)
crow::response NotAuthorized(std::string_view detail = "You do not have permission to access this resource");

// 404 Not Found - Resource doesn't exist
crow::response NotFound(std::string_view detail = "The requested resource was not found");

// 409 Conflict - Duplicate idempotent request
crow::response IdempotencyConflict(std::string_view detail = "A conflicting request is already being processed");

// 429 Too Many Requests - rate-limit gate refused the attempt.
// Phase 5.7 of the security review: returned for both the per-email
// / per-IP burst case AND the sticky account-lockout case. From the
// caller's perspective the two are indistinguishable; the operator
// gets the breakdown in the server logs.
crow::response TooManyAttempts(std::string_view detail = "Too many attempts. Please try again later.");

// 500 Internal Server Error - Server-side failures.
//
// Phase 7.3 of the security review: the response body is ALWAYS the
// generic message "An unexpected error occurred. Please try again
// later" — it never echoes the caller's argument. The argument is
// instead logged server-side as the `serverContext` so operators
// can correlate the response with the underlying error in the journal.
//
// Common pattern: pass `e.what()` from a catch block. The exception
// text lands in the log only, never the response body. This prevents
// stack-trace / SQL-error leakage to clients.
crow::response InternalError(std::string_view serverContext = "");

// Future payment errors (402 Payment Required)
crow::response PaymentDeclined(
    std::string_view detail,
    std::string_view provider = "",
    std::string_view providerCode = "");

crow::response PaymentFailed(
    std::string_view detail,
    std::string_view provider = "",
    std::string_view providerCode = "");

// Build the JSON body for an error response (useful for testing)
Json::Value BuildJsonBody(
    std::string_view type,
    std::string_view title,
    int status,
    std::string_view detail,
    const Json::JsonObject& extensions = {});

} // namespace ErrorResponse
