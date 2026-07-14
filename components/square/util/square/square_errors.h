#pragma once

#include <exception>
#include <string>

namespace Square {

// Base exception for all Square API errors
class SquareException : public std::exception {
public:
    SquareException(std::string category, std::string code, std::string detail)
        : category_(std::move(category))
        , code_(std::move(code))
        , detail_(std::move(detail)) {}

    const char* what() const noexcept override { return detail_.c_str(); }
    const std::string& GetCategory() const { return category_; }
    const std::string& GetCode() const { return code_; }
    const std::string& GetDetail() const { return detail_; }

protected:
    std::string category_;
    std::string code_;
    std::string detail_;
};

// Payment was declined (card issue, insufficient funds, CVV failure, etc.)
// Maps to ErrorResponse::PaymentDeclined()
class PaymentDeclinedException : public SquareException {
public:
    using SquareException::SquareException;
};

// Request was invalid (bad parameters, missing fields)
// Maps to ErrorResponse::ValidationError() or BadRequest()
class InvalidRequestException : public SquareException {
public:
    InvalidRequestException(std::string code, std::string detail, std::string field = "")
        : SquareException("INVALID_REQUEST_ERROR", std::move(code), std::move(detail))
        , field_(std::move(field)) {}

    const std::string& GetField() const { return field_; }

private:
    std::string field_;
};

// Authentication failed (invalid or expired token)
// Maps to ErrorResponse::NotAuthenticated() or similar
class AuthenticationException : public SquareException {
public:
    AuthenticationException(std::string code, std::string detail)
        : SquareException("AUTHENTICATION_ERROR", std::move(code), std::move(detail)) {}
};

// Rate limited - caller should retry with backoff
// Maps to 503/429 response
class RateLimitException : public SquareException {
public:
    RateLimitException(std::string detail)
        : SquareException("RATE_LIMIT_ERROR", "RATE_LIMITED", std::move(detail)) {}
};

// Square server error (5xx) - may be transient, retryable
// Maps to ErrorResponse::InternalError() or PaymentFailed()
class SquareServerException : public SquareException {
public:
    SquareServerException(std::string code, std::string detail)
        : SquareException("API_ERROR", std::move(code), std::move(detail)) {}
};

// Network/connection error - may be transient, retryable
class NetworkException : public std::exception {
public:
    explicit NetworkException(std::string message)
        : message_(std::move(message)) {}

    const char* what() const noexcept override { return message_.c_str(); }
    const std::string& GetMessage() const { return message_; }

private:
    std::string message_;
};

}  // namespace Square
