#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "util/http/http_client.h"

namespace Square {

struct PaymentResult {
    std::string paymentId;
    std::string status;
    int64_t amountCents = 0;
    std::string currency;
    std::string rawJson;
};

struct RefundResult {
    std::string refundId;
    std::string status;
    int64_t amountCents = 0;
    std::string currency;
    std::string rawJson;
};

struct CustomerResult {
    std::string customerId;
    std::string rawJson;
};

struct CreateCardResult {
    std::string cardId;
    std::string brand;
    std::string last4;
    int64_t expMonth = 0;
    int64_t expYear = 0;
    std::string rawJson;
};

struct CardInfo {
    std::string cardId;
    std::string brand;
    std::string last4;
    int64_t expMonth = 0;
    int64_t expYear = 0;
    bool enabled = false;
};

struct RetryPolicy {
    int maxRetries = 3;
    std::chrono::milliseconds initialDelay{500};
    std::chrono::milliseconds maxDelay{30000};
    double backoffMultiplier = 2.0;
    bool addJitter = true;

    static RetryPolicy Default() { return {}; }
    static RetryPolicy NoRetry() { return {0, {}, {}, 1.0, false}; }
};

class SquareClient {
public:
    virtual ~SquareClient() = default;

    /**
     * Creates a payment using the Square Payments API.
     *
     * @param sourceId The payment source (card nonce from Web Payments SDK)
     * @param amountCents Amount in cents (e.g., 1000 for $10.00)
     * @param currency ISO 4217 currency code (e.g., "USD")
     * @param idempotencyKey Unique key to prevent duplicate payments (use UUID)
     * @param note Optional note to attach to the payment
     * @param retryPolicy Policy for retrying transient failures
     * @return PaymentResult with payment details
     * @throws PaymentDeclinedException if the payment was declined
     * @throws InvalidRequestException if the request was malformed
     * @throws RateLimitException if rate limited (after retries exhausted)
     * @throws SquareServerException if Square server error (after retries exhausted)
     * @throws NetworkException if network error (after retries exhausted)
     */
    virtual PaymentResult CreatePayment(
        const std::string& sourceId,
        int64_t amountCents,
        const std::string& currency,
        const std::string& idempotencyKey,
        const std::string& note = "",
        const std::string& customerId = "",
        const RetryPolicy& retryPolicy = RetryPolicy::Default()) = 0;

    virtual CustomerResult CreateCustomer(
        const std::string& idempotencyKey,
        const std::string& email,
        const std::string& firstName,
        const std::string& lastName) = 0;

    virtual CustomerResult GetCustomer(const std::string& customerId) = 0;

    virtual CreateCardResult CreateCard(
        const std::string& idempotencyKey,
        const std::string& sourceId,
        const std::string& customerId) = 0;

    virtual std::vector<CardInfo> ListCards(const std::string& customerId) = 0;

    virtual void DisableCard(const std::string& cardId) = 0;

    virtual RefundResult RefundPayment(
        const std::string& paymentId,
        int64_t amountCents,
        const std::string& currency,
        const std::string& idempotencyKey,
        const std::string& reason = "",
        const RetryPolicy& retryPolicy = RetryPolicy::Default()) = 0;

protected:
    SquareClient() = default;
    SquareClient(const SquareClient&) = default;
    SquareClient& operator=(const SquareClient&) = default;
};

using SquareClientPtr = std::shared_ptr<SquareClient>;

/**
 * Creates a production SquareClient.
 *
 * @param httpClient HTTP client for making requests
 * @param accessToken Square API access token
 * @param sandbox True for sandbox environment, false for production
 */
SquareClientPtr MakeSquareClient(
    Http::HttpClientPtr httpClient,
    std::string accessToken,
    bool sandbox);

}  // namespace Square
