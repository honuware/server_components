#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "square_client.h"
#include "square_client_test_util.h"
#include "square_errors.h"
#include "util/http/http_client_test_util.h"

namespace {

using ::testing::HasSubstr;

TEST(SquareClientTest, CreatePaymentSuccess) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-access-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(200, R"({
        "payment": {
            "id": "pay_abc123",
            "status": "COMPLETED",
            "amount_money": {
                "amount": 1000,
                "currency": "USD"
            }
        }
    })");

    auto result = squareClient->CreatePayment(
        "cnon:card-nonce-ok",
        1000,
        "USD",
        "idempotency-key-123",
        "Test payment",
        "",
        Square::RetryPolicy::NoRetry());

    EXPECT_EQ(result.paymentId, "pay_abc123");
    EXPECT_EQ(result.status, "COMPLETED");
    EXPECT_EQ(result.amountCents, 1000);
    EXPECT_EQ(result.currency, "USD");

    ASSERT_EQ(testHttpClient->GetRequestCount(), 1);
    const auto& request = testHttpClient->GetRequests()[0];

    EXPECT_EQ(request.url, "https://connect.squareupsandbox.com/v2/payments");
    EXPECT_EQ(request.method, "POST");
    EXPECT_EQ(request.headers.at("Authorization"), "Bearer test-access-token");
    EXPECT_EQ(request.headers.at("Content-Type"), "application/json");
    EXPECT_THAT(request.headers.at("Square-Version"), HasSubstr("202"));

    EXPECT_THAT(request.body, HasSubstr("cnon:card-nonce-ok"));
    EXPECT_THAT(request.body, HasSubstr("idempotency-key-123"));
    EXPECT_THAT(request.body, HasSubstr("1000"));
    EXPECT_THAT(request.body, HasSubstr("USD"));
    EXPECT_THAT(request.body, HasSubstr("Test payment"));
}

TEST(SquareClientTest, CreatePaymentProductionUrl) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto prodClient = Square::MakeSquareClient(
        testHttpClient, "prod-token", /*sandbox=*/false);

    testHttpClient->SetNextResponse(200, R"({
        "payment": {
            "id": "pay_prod",
            "status": "COMPLETED",
            "amount_money": {"amount": 500, "currency": "USD"}
        }
    })");

    prodClient->CreatePayment("nonce", 500, "USD", "key", "", "",
        Square::RetryPolicy::NoRetry());

    ASSERT_EQ(testHttpClient->GetRequestCount(), 1);
    EXPECT_EQ(testHttpClient->GetRequests()[0].url,
              "https://connect.squareup.com/v2/payments");
}

TEST(SquareClientTest, CreatePaymentCardDeclined) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(400, R"({
        "errors": [{
            "category": "PAYMENT_METHOD_ERROR",
            "code": "CARD_DECLINED",
            "detail": "The card was declined."
        }]
    })");

    EXPECT_THROW({
        squareClient->CreatePayment("bad-nonce", 1000, "USD", "key", "", "",
            Square::RetryPolicy::NoRetry());
    }, Square::PaymentDeclinedException);
}

TEST(SquareClientTest, CreatePaymentCvvFailure) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(400, R"({
        "errors": [{
            "category": "PAYMENT_METHOD_ERROR",
            "code": "CVV_FAILURE",
            "detail": "The CVV did not match."
        }]
    })");

    try {
        squareClient->CreatePayment("bad-nonce", 1000, "USD", "key", "", "",
            Square::RetryPolicy::NoRetry());
        FAIL() << "Expected PaymentDeclinedException";
    } catch (const Square::PaymentDeclinedException& e) {
        EXPECT_EQ(e.GetCode(), "CVV_FAILURE");
        EXPECT_THAT(e.GetDetail(), HasSubstr("CVV"));
    }
}

TEST(SquareClientTest, CreatePaymentInvalidRequest) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(400, R"({
        "errors": [{
            "category": "INVALID_REQUEST_ERROR",
            "code": "MISSING_REQUIRED_PARAMETER",
            "detail": "Missing required parameter: amount_money",
            "field": "amount_money"
        }]
    })");

    try {
        squareClient->CreatePayment("nonce", 0, "USD", "key", "", "",
            Square::RetryPolicy::NoRetry());
        FAIL() << "Expected InvalidRequestException";
    } catch (const Square::InvalidRequestException& e) {
        EXPECT_EQ(e.GetCode(), "MISSING_REQUIRED_PARAMETER");
        EXPECT_EQ(e.GetField(), "amount_money");
    }
}

TEST(SquareClientTest, CreatePaymentAuthenticationError) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "bad-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(401, R"({
        "errors": [{
            "category": "AUTHENTICATION_ERROR",
            "code": "UNAUTHORIZED",
            "detail": "The access token is invalid or has expired."
        }]
    })");

    EXPECT_THROW({
        squareClient->CreatePayment("nonce", 1000, "USD", "key", "", "",
            Square::RetryPolicy::NoRetry());
    }, Square::AuthenticationException);
}

TEST(SquareClientTest, CreatePaymentServerErrorNoRetry) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(500, R"({
        "errors": [{
            "category": "API_ERROR",
            "code": "INTERNAL_SERVER_ERROR",
            "detail": "An internal error occurred."
        }]
    })");

    EXPECT_THROW({
        squareClient->CreatePayment("nonce", 1000, "USD", "key", "", "",
            Square::RetryPolicy::NoRetry());
    }, Square::SquareServerException);

    EXPECT_EQ(testHttpClient->GetRequestCount(), 1);
}

TEST(SquareClientTest, CreatePaymentRateLimitNoRetry) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(429, R"({
        "errors": [{
            "category": "RATE_LIMIT_ERROR",
            "code": "RATE_LIMITED",
            "detail": "Too many requests."
        }]
    })");

    EXPECT_THROW({
        squareClient->CreatePayment("nonce", 1000, "USD", "key", "", "",
            Square::RetryPolicy::NoRetry());
    }, Square::RateLimitException);

    EXPECT_EQ(testHttpClient->GetRequestCount(), 1);
}

TEST(SquareClientTest, CreatePaymentRetriesOnServerError) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    // First two calls fail with 500, third succeeds
    testHttpClient->QueueResponse(500, R"({"errors":[{"category":"API_ERROR","code":"ERROR","detail":"fail"}]})");
    testHttpClient->QueueResponse(500, R"({"errors":[{"category":"API_ERROR","code":"ERROR","detail":"fail"}]})");
    testHttpClient->QueueResponse(200, R"({
        "payment": {
            "id": "pay_retry_success",
            "status": "COMPLETED",
            "amount_money": {"amount": 1000, "currency": "USD"}
        }
    })");

    Square::RetryPolicy fastRetry;
    fastRetry.maxRetries = 3;
    fastRetry.initialDelay = std::chrono::milliseconds(1);
    fastRetry.maxDelay = std::chrono::milliseconds(10);
    fastRetry.addJitter = false;

    auto result = squareClient->CreatePayment(
        "nonce", 1000, "USD", "key", "", "", fastRetry);

    EXPECT_EQ(result.paymentId, "pay_retry_success");
    EXPECT_EQ(testHttpClient->GetRequestCount(), 3);
}

TEST(SquareClientTest, CreatePaymentExhaustsRetries) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    // All retries fail
    for (int i = 0; i < 4; i++) {
        testHttpClient->QueueResponse(500, R"({"errors":[{"category":"API_ERROR","code":"ERROR","detail":"fail"}]})");
    }

    Square::RetryPolicy fastRetry;
    fastRetry.maxRetries = 3;
    fastRetry.initialDelay = std::chrono::milliseconds(1);
    fastRetry.maxDelay = std::chrono::milliseconds(10);
    fastRetry.addJitter = false;

    EXPECT_THROW({
        squareClient->CreatePayment("nonce", 1000, "USD", "key", "", "", fastRetry);
    }, Square::SquareServerException);

    // Should have made 4 requests (initial + 3 retries)
    EXPECT_EQ(testHttpClient->GetRequestCount(), 4);
}

TEST(SquareClientTest, CreatePaymentWithCustomerId) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(200, R"({
        "payment": {
            "id": "pay_saved_card",
            "status": "COMPLETED",
            "amount_money": {"amount": 2000, "currency": "USD"}
        }
    })");

    auto result = squareClient->CreatePayment(
        "ccof:card123", 2000, "USD", "idem-key", "Saved card payment",
        "cust_abc123", Square::RetryPolicy::NoRetry());

    EXPECT_EQ(result.paymentId, "pay_saved_card");

    ASSERT_EQ(testHttpClient->GetRequestCount(), 1);
    const auto& request = testHttpClient->GetRequests()[0];
    EXPECT_THAT(request.body, HasSubstr("ccof:card123"));
    EXPECT_THAT(request.body, HasSubstr("cust_abc123"));
}

// CreateCustomer tests

TEST(SquareClientTest, CreateCustomerSuccess) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(200, R"({
        "customer": {
            "id": "cust_new123",
            "given_name": "Test",
            "family_name": "User",
            "email_address": "test@example.com"
        }
    })");

    auto result = squareClient->CreateCustomer(
        "idem-key", "test@example.com", "Test", "User");

    EXPECT_EQ(result.customerId, "cust_new123");
    EXPECT_FALSE(result.rawJson.empty());

    ASSERT_EQ(testHttpClient->GetRequestCount(), 1);
    const auto& request = testHttpClient->GetRequests()[0];
    EXPECT_EQ(request.url, "https://connect.squareupsandbox.com/v2/customers");
    EXPECT_EQ(request.method, "POST");
    EXPECT_THAT(request.body, HasSubstr("test@example.com"));
    EXPECT_THAT(request.body, HasSubstr("Test"));
    EXPECT_THAT(request.body, HasSubstr("User"));
}

TEST(SquareClientTest, CreateCustomerInvalidRequest) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(400, R"({
        "errors": [{
            "category": "INVALID_REQUEST_ERROR",
            "code": "INVALID_EMAIL_ADDRESS",
            "detail": "The email address is invalid.",
            "field": "email_address"
        }]
    })");

    EXPECT_THROW({
        squareClient->CreateCustomer("key", "bad-email", "Test", "User");
    }, Square::InvalidRequestException);
}

// GetCustomer tests

TEST(SquareClientTest, GetCustomerSuccess) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(200, R"({
        "customer": {
            "id": "cust_existing",
            "given_name": "Existing",
            "family_name": "User"
        }
    })");

    auto result = squareClient->GetCustomer("cust_existing");

    EXPECT_EQ(result.customerId, "cust_existing");

    ASSERT_EQ(testHttpClient->GetRequestCount(), 1);
    const auto& request = testHttpClient->GetRequests()[0];
    EXPECT_EQ(request.url,
              "https://connect.squareupsandbox.com/v2/customers/cust_existing");
    EXPECT_EQ(request.method, "GET");
}

TEST(SquareClientTest, GetCustomerNotFound) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(404, R"({
        "errors": [{
            "category": "INVALID_REQUEST_ERROR",
            "code": "NOT_FOUND",
            "detail": "Customer not found."
        }]
    })");

    EXPECT_THROW({
        squareClient->GetCustomer("nonexistent");
    }, Square::InvalidRequestException);
}

// CreateCard tests

TEST(SquareClientTest, CreateCardSuccess) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(200, R"({
        "card": {
            "id": "ccof:card_abc123",
            "card_brand": "VISA",
            "last_4": "4242",
            "exp_month": 12,
            "exp_year": 2028
        }
    })");

    auto result = squareClient->CreateCard(
        "idem-key", "cnon:card-nonce", "cust_123");

    EXPECT_EQ(result.cardId, "ccof:card_abc123");
    EXPECT_EQ(result.brand, "VISA");
    EXPECT_EQ(result.last4, "4242");
    EXPECT_EQ(result.expMonth, 12);
    EXPECT_EQ(result.expYear, 2028);
    EXPECT_FALSE(result.rawJson.empty());

    ASSERT_EQ(testHttpClient->GetRequestCount(), 1);
    const auto& request = testHttpClient->GetRequests()[0];
    EXPECT_EQ(request.url, "https://connect.squareupsandbox.com/v2/cards");
    EXPECT_EQ(request.method, "POST");
    EXPECT_THAT(request.body, HasSubstr("cnon:card-nonce"));
    EXPECT_THAT(request.body, HasSubstr("cust_123"));
}

// ListCards tests

TEST(SquareClientTest, ListCardsSuccess) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(200, R"({
        "cards": [
            {
                "id": "ccof:card1",
                "card_brand": "VISA",
                "last_4": "1111",
                "exp_month": 3,
                "exp_year": 2027,
                "enabled": true
            },
            {
                "id": "ccof:card2",
                "card_brand": "MASTERCARD",
                "last_4": "2222",
                "exp_month": 6,
                "exp_year": 2029,
                "enabled": false
            }
        ]
    })");

    auto cards = squareClient->ListCards("cust_123");

    ASSERT_EQ(cards.size(), 2u);
    EXPECT_EQ(cards[0].cardId, "ccof:card1");
    EXPECT_EQ(cards[0].brand, "VISA");
    EXPECT_EQ(cards[0].last4, "1111");
    EXPECT_EQ(cards[0].expMonth, 3);
    EXPECT_EQ(cards[0].expYear, 2027);
    EXPECT_TRUE(cards[0].enabled);
    EXPECT_EQ(cards[1].cardId, "ccof:card2");
    EXPECT_EQ(cards[1].brand, "MASTERCARD");
    EXPECT_FALSE(cards[1].enabled);

    ASSERT_EQ(testHttpClient->GetRequestCount(), 1);
    const auto& request = testHttpClient->GetRequests()[0];
    EXPECT_EQ(request.url,
              "https://connect.squareupsandbox.com/v2/cards?customer_id=cust_123");
    EXPECT_EQ(request.method, "GET");
}

TEST(SquareClientTest, ListCardsEmpty) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(200, R"({})");

    auto cards = squareClient->ListCards("cust_no_cards");

    EXPECT_TRUE(cards.empty());
}

// DisableCard tests

TEST(SquareClientTest, DisableCardSuccess) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(200, R"({
        "card": {
            "id": "ccof:card_to_disable",
            "enabled": false
        }
    })");

    EXPECT_NO_THROW(squareClient->DisableCard("ccof:card_to_disable"));

    ASSERT_EQ(testHttpClient->GetRequestCount(), 1);
    const auto& request = testHttpClient->GetRequests()[0];
    EXPECT_EQ(request.url,
              "https://connect.squareupsandbox.com/v2/cards/ccof:card_to_disable/disable");
    EXPECT_EQ(request.method, "POST");
}

TEST(SquareClientTest, DisableCardNotFound) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(404, R"({
        "errors": [{
            "category": "INVALID_REQUEST_ERROR",
            "code": "NOT_FOUND",
            "detail": "Card not found."
        }]
    })");

    EXPECT_THROW({
        squareClient->DisableCard("nonexistent");
    }, Square::InvalidRequestException);
}

TEST(SquareClientTest, RefundPaymentSuccess) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(200, R"({
        "refund": {
            "id": "refund_abc123",
            "status": "COMPLETED",
            "amount_money": {
                "amount": 5000,
                "currency": "USD"
            },
            "payment_id": "payment_xyz"
        }
    })");

    auto result = squareClient->RefundPayment(
        "payment_xyz", 5000, "USD", "idempotency-key-1", "Customer cancellation");

    EXPECT_EQ(result.refundId, "refund_abc123");
    EXPECT_EQ(result.status, "COMPLETED");
    EXPECT_EQ(result.amountCents, 5000);
    EXPECT_EQ(result.currency, "USD");

    // Verify HTTP call
    const auto& requests = testHttpClient->GetRequests();
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_THAT(requests[0].url, HasSubstr("/refunds"));
    EXPECT_EQ(requests[0].method, "POST");
    EXPECT_THAT(requests[0].body, HasSubstr("payment_xyz"));
    EXPECT_THAT(requests[0].body, HasSubstr("idempotency-key-1"));
    EXPECT_THAT(requests[0].body, HasSubstr("Customer cancellation"));
}

TEST(SquareClientTest, RefundPaymentPartial) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(200, R"({
        "refund": {
            "id": "refund_partial",
            "status": "COMPLETED",
            "amount_money": {
                "amount": 2500,
                "currency": "USD"
            }
        }
    })");

    auto result = squareClient->RefundPayment(
        "payment_xyz", 2500, "USD", "key-2");

    EXPECT_EQ(result.refundId, "refund_partial");
    EXPECT_EQ(result.amountCents, 2500);
}

TEST(SquareClientTest, RefundPaymentInvalidRequest) {
    auto testHttpClient = Http::Test::MakeTestHttpClient();
    auto squareClient = Square::MakeSquareClient(
        testHttpClient, "test-token", /*sandbox=*/true);

    testHttpClient->SetNextResponse(400, R"({
        "errors": [{
            "category": "INVALID_REQUEST_ERROR",
            "code": "REFUND_ALREADY_PENDING",
            "detail": "A refund is already pending for this payment."
        }]
    })");

    EXPECT_THROW({
        squareClient->RefundPayment(
            "payment_xyz", 5000, "USD", "key-3");
    }, Square::InvalidRequestException);
}

}  // namespace
