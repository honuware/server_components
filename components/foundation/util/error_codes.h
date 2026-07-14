#pragma once

#include <string_view>

namespace ErrorCodes {

// Error type constants (RFC 7807 "type" field values)
constexpr std::string_view kValidationError = "validation_error";
constexpr std::string_view kBadRequest = "bad_request";
constexpr std::string_view kNotAuthenticated = "not_authenticated";
constexpr std::string_view kInvalidCredentials = "invalid_credentials";
constexpr std::string_view kSessionExpired = "session_expired";
constexpr std::string_view kNotAuthorized = "not_authorized";
constexpr std::string_view kNotFound = "not_found";
constexpr std::string_view kIdempotencyConflict = "idempotency_conflict";
// Phase 5.7 of the security review: returned when the rate-limit
// gate (or sticky account lockout) refuses an authentication
// attempt. Distinct from kInvalidCredentials so the SPA can render
// a "try again later" message; from a security standpoint both
// look the same to an attacker (no detail about WHY 429).
constexpr std::string_view kTooManyAttempts = "too_many_attempts";
constexpr std::string_view kInternalError = "internal_error";

// Future payment error types
constexpr std::string_view kPaymentDeclined = "payment_declined";
constexpr std::string_view kPaymentFailed = "payment_failed";
constexpr std::string_view kVoucherInvalid = "voucher_invalid";
constexpr std::string_view kSeatsExceeded = "seats_exceeded";

// Human-readable titles for each error type
inline std::string_view GetTitleForType(std::string_view type) {
    if (type == kValidationError) return "Validation Error";
    if (type == kBadRequest) return "Bad Request";
    if (type == kNotAuthenticated) return "Authentication Required";
    if (type == kInvalidCredentials) return "Invalid Credentials";
    if (type == kSessionExpired) return "Session Expired";
    if (type == kNotAuthorized) return "Not Authorized";
    if (type == kNotFound) return "Not Found";
    if (type == kIdempotencyConflict) return "Conflict";
    if (type == kTooManyAttempts) return "Too Many Attempts";
    if (type == kInternalError) return "Internal Server Error";
    if (type == kPaymentDeclined) return "Payment Declined";
    if (type == kPaymentFailed) return "Payment Failed";
    if (type == kVoucherInvalid) return "Invalid Voucher";
    if (type == kSeatsExceeded) return "Seats Exceeded";
    return "Error";
}

// HTTP status codes for each error type
inline int GetStatusForType(std::string_view type) {
    if (type == kValidationError) return 400;
    if (type == kBadRequest) return 400;
    if (type == kNotAuthenticated) return 401;
    if (type == kInvalidCredentials) return 401;
    if (type == kSessionExpired) return 401;
    if (type == kNotAuthorized) return 403;
    if (type == kNotFound) return 404;
    if (type == kIdempotencyConflict) return 409;
    if (type == kTooManyAttempts) return 429;
    if (type == kInternalError) return 500;
    if (type == kPaymentDeclined) return 402;
    if (type == kPaymentFailed) return 402;
    if (type == kVoucherInvalid) return 400;
    if (type == kSeatsExceeded) return 400;
    return 500;
}

} // namespace ErrorCodes
