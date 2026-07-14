#include "square_client.h"
#include "square_errors.h"

#include "util/json_value.h"

#include <chrono>
#include <random>
#include <thread>

namespace Square {

namespace {

constexpr std::string_view kApiVersion = "2024-01-18";
constexpr std::string_view kSandboxBaseUrl = "https://connect.squareupsandbox.com/v2";
constexpr std::string_view kProductionBaseUrl = "https://connect.squareup.com/v2";

// Determine if an error is retryable based on status code
bool IsRetryableStatusCode(int statusCode) {
    // 429 (rate limit) and 5xx (server errors) are retryable
    return statusCode == 429 || (statusCode >= 500 && statusCode < 600);
}

// Calculate delay for exponential backoff with optional jitter
std::chrono::milliseconds CalculateBackoffDelay(
    int attempt,
    const RetryPolicy& policy) {

    // Calculate exponential delay
    double delayMs = static_cast<double>(policy.initialDelay.count()) *
                     std::pow(policy.backoffMultiplier, attempt);

    // Cap at max delay
    delayMs = std::min(delayMs, static_cast<double>(policy.maxDelay.count()));

    // Add jitter (0-10% of delay)
    if (policy.addJitter && delayMs > 0) {
        static thread_local std::mt19937 gen(std::random_device{}());
        std::uniform_real_distribution<> dis(0.0, 0.1);
        delayMs *= (1.0 + dis(gen));
    }

    return std::chrono::milliseconds(static_cast<int64_t>(delayMs));
}

// Parse Square API error response and throw appropriate exception
void ThrowSquareError(int statusCode, const Json::Value& errorJson) {
    std::string category = "UNKNOWN";
    std::string code = "UNKNOWN";
    std::string detail = "An unknown error occurred";
    std::string field;

    // Parse the first error from the errors array
    if (errorJson.HasChildren()) {
        const Json::Value* errorsArray = nullptr;
        if (errorJson.HasChild("errors", &errorsArray) && errorsArray->IsArray()) {
            const auto& errors = errorsArray->GetArray();
            if (!errors.empty()) {
                const auto& firstError = errors[0];
                if (firstError.HasChildren()) {
                    const Json::Value* val = nullptr;
                    if (firstError.HasChild("category", &val)) {
                        category = val->ToSimpleString();
                    }
                    if (firstError.HasChild("code", &val)) {
                        code = val->ToSimpleString();
                    }
                    if (firstError.HasChild("detail", &val)) {
                        detail = val->ToSimpleString();
                    }
                    if (firstError.HasChild("field", &val)) {
                        field = val->ToSimpleString();
                    }
                }
            }
        }
    }

    // Map to appropriate exception type
    if (category == "PAYMENT_METHOD_ERROR") {
        throw PaymentDeclinedException(category, code, detail);
    }
    if (category == "INVALID_REQUEST_ERROR") {
        throw InvalidRequestException(code, detail, field);
    }
    if (category == "AUTHENTICATION_ERROR") {
        throw AuthenticationException(code, detail);
    }
    if (category == "RATE_LIMIT_ERROR" || statusCode == 429) {
        throw RateLimitException(detail);
    }
    if (statusCode >= 500) {
        throw SquareServerException(code, detail);
    }

    // Default to invalid request for other 4xx errors
    throw InvalidRequestException(code, detail, field);
}

class SquareClientImpl : public SquareClient {
public:
    SquareClientImpl(Http::HttpClientPtr httpClient, std::string accessToken, bool sandbox)
        : httpClient_(std::move(httpClient))
        , accessToken_(std::move(accessToken))
        , baseUrl_(sandbox ? std::string(kSandboxBaseUrl) : std::string(kProductionBaseUrl)) {}

    PaymentResult CreatePayment(
        const std::string& sourceId,
        int64_t amountCents,
        const std::string& currency,
        const std::string& idempotencyKey,
        const std::string& note,
        const std::string& customerId,
        const RetryPolicy& retryPolicy) override {

        Json::JsonObject amountMoney;
        amountMoney["amount"] = amountCents;
        amountMoney["currency"] = currency;

        Json::JsonObject body;
        body["source_id"] = sourceId;
        body["idempotency_key"] = idempotencyKey;
        body["amount_money"] = Json::Value(std::move(amountMoney));

        if (!note.empty()) {
            body["note"] = note;
        }
        if (!customerId.empty()) {
            body["customer_id"] = customerId;
        }

        Json::Value bodyJson(std::move(body));

        return ExecuteWithRetry(
            "/payments",
            bodyJson.ToString(),
            retryPolicy,
            [](const Json::Value& response) {
                PaymentResult result;

                const Json::Value* payment = nullptr;
                if (!response.HasChild("payment", &payment)) {
                    throw SquareServerException("INVALID_RESPONSE",
                        "Missing 'payment' in response");
                }

                const Json::Value* val = nullptr;
                if (payment->HasChild("id", &val)) {
                    result.paymentId = val->ToSimpleString();
                }
                if (payment->HasChild("status", &val)) {
                    result.status = val->ToSimpleString();
                }

                const Json::Value* amountMoney = nullptr;
                if (payment->HasChild("amount_money", &amountMoney)) {
                    if (amountMoney->HasChild("amount", &val)) {
                        result.amountCents = val->Get<int64_t>();
                    }
                    if (amountMoney->HasChild("currency", &val)) {
                        result.currency = val->ToSimpleString();
                    }
                }

                result.rawJson = response.ToString();
                return result;
            });
    }

    CustomerResult CreateCustomer(
        const std::string& idempotencyKey,
        const std::string& email,
        const std::string& firstName,
        const std::string& lastName) override {

        Json::JsonObject body;
        body["idempotency_key"] = idempotencyKey;
        body["email_address"] = email;
        body["given_name"] = firstName;
        body["family_name"] = lastName;

        Json::Value bodyJson(std::move(body));

        return ExecuteWithRetry(
            "/customers",
            bodyJson.ToString(),
            RetryPolicy::Default(),
            [](const Json::Value& response) {
                CustomerResult result;

                const Json::Value* customer = nullptr;
                if (!response.HasChild("customer", &customer)) {
                    throw SquareServerException("INVALID_RESPONSE",
                        "Missing 'customer' in response");
                }

                const Json::Value* val = nullptr;
                if (customer->HasChild("id", &val)) {
                    result.customerId = val->ToSimpleString();
                }

                result.rawJson = response.ToString();
                return result;
            });
    }

    CustomerResult GetCustomer(const std::string& customerId) override {
        return ExecuteGetWithRetry(
            "/customers/" + customerId,
            RetryPolicy::Default(),
            [](const Json::Value& response) {
                CustomerResult result;

                const Json::Value* customer = nullptr;
                if (!response.HasChild("customer", &customer)) {
                    throw SquareServerException("INVALID_RESPONSE",
                        "Missing 'customer' in response");
                }

                const Json::Value* val = nullptr;
                if (customer->HasChild("id", &val)) {
                    result.customerId = val->ToSimpleString();
                }

                result.rawJson = response.ToString();
                return result;
            });
    }

    CreateCardResult CreateCard(
        const std::string& idempotencyKey,
        const std::string& sourceId,
        const std::string& customerId) override {

        Json::JsonObject card;
        card["customer_id"] = customerId;

        Json::JsonObject body;
        body["idempotency_key"] = idempotencyKey;
        body["source_id"] = sourceId;
        body["card"] = Json::Value(std::move(card));

        Json::Value bodyJson(std::move(body));

        return ExecuteWithRetry(
            "/cards",
            bodyJson.ToString(),
            RetryPolicy::Default(),
            [](const Json::Value& response) {
                return ParseCardResult(response);
            });
    }

    std::vector<CardInfo> ListCards(const std::string& customerId) override {
        return ExecuteGetWithRetry(
            "/cards?customer_id=" + customerId,
            RetryPolicy::Default(),
            [](const Json::Value& response) {
                std::vector<CardInfo> cards;

                const Json::Value* cardsArray = nullptr;
                if (!response.HasChild("cards", &cardsArray) || !cardsArray->IsArray()) {
                    return cards;
                }

                for (const auto& cardJson : cardsArray->GetArray()) {
                    CardInfo card;
                    const Json::Value* val = nullptr;
                    if (cardJson.HasChild("id", &val)) {
                        card.cardId = val->ToSimpleString();
                    }
                    if (cardJson.HasChild("card_brand", &val)) {
                        card.brand = val->ToSimpleString();
                    }
                    if (cardJson.HasChild("last_4", &val)) {
                        card.last4 = val->ToSimpleString();
                    }
                    if (cardJson.HasChild("exp_month", &val)) {
                        card.expMonth = val->Get<int64_t>();
                    }
                    if (cardJson.HasChild("exp_year", &val)) {
                        card.expYear = val->Get<int64_t>();
                    }
                    if (cardJson.HasChild("enabled", &val)) {
                        card.enabled = val->Get<bool>();
                    }
                    cards.push_back(std::move(card));
                }

                return cards;
            });
    }

    void DisableCard(const std::string& cardId) override {
        ExecuteWithRetry(
            "/cards/" + cardId + "/disable",
            "{}",
            RetryPolicy::Default(),
            [](const Json::Value&) {
                return 0;
            });
    }

    RefundResult RefundPayment(
        const std::string& paymentId,
        int64_t amountCents,
        const std::string& currency,
        const std::string& idempotencyKey,
        const std::string& reason,
        const RetryPolicy& retryPolicy) override {

        Json::JsonObject amountMoney;
        amountMoney["amount"] = amountCents;
        amountMoney["currency"] = currency;

        Json::JsonObject body;
        body["payment_id"] = paymentId;
        body["idempotency_key"] = idempotencyKey;
        body["amount_money"] = Json::Value(std::move(amountMoney));

        if (!reason.empty()) {
            body["reason"] = reason;
        }

        Json::Value bodyJson(std::move(body));

        return ExecuteWithRetry(
            "/refunds",
            bodyJson.ToString(),
            retryPolicy,
            [](const Json::Value& response) {
                RefundResult result;

                const Json::Value* refund = nullptr;
                if (!response.HasChild("refund", &refund)) {
                    throw SquareServerException("INVALID_RESPONSE",
                        "Missing 'refund' in response");
                }

                const Json::Value* val = nullptr;
                if (refund->HasChild("id", &val)) {
                    result.refundId = val->ToSimpleString();
                }
                if (refund->HasChild("status", &val)) {
                    result.status = val->ToSimpleString();
                }

                const Json::Value* amountMoney = nullptr;
                if (refund->HasChild("amount_money", &amountMoney)) {
                    if (amountMoney->HasChild("amount", &val)) {
                        result.amountCents = val->Get<int64_t>();
                    }
                    if (amountMoney->HasChild("currency", &val)) {
                        result.currency = val->ToSimpleString();
                    }
                }

                result.rawJson = response.ToString();
                return result;
            });
    }

private:
    Http::HttpClientPtr httpClient_;
    std::string accessToken_;
    std::string baseUrl_;

    static CreateCardResult ParseCardResult(const Json::Value& response) {
        CreateCardResult result;

        const Json::Value* card = nullptr;
        if (!response.HasChild("card", &card)) {
            throw SquareServerException("INVALID_RESPONSE",
                "Missing 'card' in response");
        }

        const Json::Value* val = nullptr;
        if (card->HasChild("id", &val)) {
            result.cardId = val->ToSimpleString();
        }
        if (card->HasChild("card_brand", &val)) {
            result.brand = val->ToSimpleString();
        }
        if (card->HasChild("last_4", &val)) {
            result.last4 = val->ToSimpleString();
        }
        if (card->HasChild("exp_month", &val)) {
            result.expMonth = val->Get<int64_t>();
        }
        if (card->HasChild("exp_year", &val)) {
            result.expYear = val->Get<int64_t>();
        }

        result.rawJson = response.ToString();
        return result;
    }

    template<typename ResultParser>
    auto ExecuteGetWithRetry(
        const std::string& endpoint,
        const RetryPolicy& retryPolicy,
        ResultParser parseResult) -> decltype(parseResult(std::declval<Json::Value>())) {

        int attempt = 0;

        while (true) {
            try {
                Http::HttpRequest request;
                request.url = baseUrl_ + endpoint;
                request.method = "GET";
                request.headers = {
                    {"Authorization", "Bearer " + accessToken_},
                    {"Accept", "application/json"},
                    {"Square-Version", std::string(kApiVersion)}
                };

                auto response = httpClient_->Execute(request);
                auto json = Json::Value::FromText(response.body);

                if (response.statusCode >= 400) {
                    if (IsRetryableStatusCode(response.statusCode) &&
                        attempt < retryPolicy.maxRetries) {
                        auto delay = CalculateBackoffDelay(attempt, retryPolicy);
                        std::this_thread::sleep_for(delay);
                        attempt++;
                        continue;
                    }
                    ThrowSquareError(response.statusCode, json);
                }

                return parseResult(json);

            } catch (const NetworkException&) {
                if (attempt < retryPolicy.maxRetries) {
                    auto delay = CalculateBackoffDelay(attempt, retryPolicy);
                    std::this_thread::sleep_for(delay);
                    attempt++;
                    continue;
                }
                throw;
            } catch (const RateLimitException&) {
                if (attempt < retryPolicy.maxRetries) {
                    auto delay = CalculateBackoffDelay(attempt, retryPolicy);
                    std::this_thread::sleep_for(delay);
                    attempt++;
                    continue;
                }
                throw;
            } catch (const SquareServerException&) {
                if (attempt < retryPolicy.maxRetries) {
                    auto delay = CalculateBackoffDelay(attempt, retryPolicy);
                    std::this_thread::sleep_for(delay);
                    attempt++;
                    continue;
                }
                throw;
            }
        }
    }

    template<typename ResultParser>
    auto ExecuteWithRetry(
        const std::string& endpoint,
        const std::string& body,
        const RetryPolicy& retryPolicy,
        ResultParser parseResult) -> decltype(parseResult(std::declval<Json::Value>())) {

        int attempt = 0;
        std::exception_ptr lastException;

        while (true) {
            try {
                Http::HttpRequest request;
                request.url = baseUrl_ + endpoint;
                request.method = "POST";
                request.body = body;
                request.headers = {
                    {"Authorization", "Bearer " + accessToken_},
                    {"Content-Type", "application/json"},
                    {"Square-Version", std::string(kApiVersion)}
                };

                auto response = httpClient_->Execute(request);
                auto json = Json::Value::FromText(response.body);

                if (response.statusCode >= 400) {
                    // Check if retryable
                    if (IsRetryableStatusCode(response.statusCode) &&
                        attempt < retryPolicy.maxRetries) {
                        // Sleep and retry
                        auto delay = CalculateBackoffDelay(attempt, retryPolicy);
                        std::this_thread::sleep_for(delay);
                        attempt++;
                        continue;
                    }
                    // Not retryable or retries exhausted
                    ThrowSquareError(response.statusCode, json);
                }

                return parseResult(json);

            } catch (const NetworkException&) {
                // Network errors are retryable
                lastException = std::current_exception();
                if (attempt < retryPolicy.maxRetries) {
                    auto delay = CalculateBackoffDelay(attempt, retryPolicy);
                    std::this_thread::sleep_for(delay);
                    attempt++;
                    continue;
                }
                throw;
            } catch (const RateLimitException&) {
                // Rate limit is retryable
                lastException = std::current_exception();
                if (attempt < retryPolicy.maxRetries) {
                    auto delay = CalculateBackoffDelay(attempt, retryPolicy);
                    std::this_thread::sleep_for(delay);
                    attempt++;
                    continue;
                }
                throw;
            } catch (const SquareServerException&) {
                // Server errors are retryable
                lastException = std::current_exception();
                if (attempt < retryPolicy.maxRetries) {
                    auto delay = CalculateBackoffDelay(attempt, retryPolicy);
                    std::this_thread::sleep_for(delay);
                    attempt++;
                    continue;
                }
                throw;
            }
            // Other exceptions (PaymentDeclined, InvalidRequest, Authentication)
            // are not retryable
        }
    }
};

}  // namespace

SquareClientPtr MakeSquareClient(
    Http::HttpClientPtr httpClient,
    std::string accessToken,
    bool sandbox) {
    return std::make_shared<SquareClientImpl>(
        std::move(httpClient), std::move(accessToken), sandbox);
}

}  // namespace Square
