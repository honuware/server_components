#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "square_client_test_util.h"
#include "square_errors.h"

namespace {

using ::testing::HasSubstr;

TEST(TestSquareClientTest, RecordsCallArguments) {
    auto testClient = Square::Test::MakeTestSquareClient();

    Square::PaymentResult result;
    result.paymentId = "pay_test";
    result.status = "COMPLETED";
    result.amountCents = 2500;
    result.currency = "USD";
    testClient->SetNextPaymentResult(result);

    testClient->CreatePayment("nonce123", 2500, "USD", "idem-key", "A note",
        "", Square::RetryPolicy::NoRetry());

    ASSERT_EQ(testClient->GetCreatePaymentCallCount(), 1);
    const auto& call = testClient->GetCreatePaymentCalls()[0];
    EXPECT_EQ(call.sourceId, "nonce123");
    EXPECT_EQ(call.amountCents, 2500);
    EXPECT_EQ(call.currency, "USD");
    EXPECT_EQ(call.idempotencyKey, "idem-key");
    EXPECT_EQ(call.note, "A note");
}

TEST(TestSquareClientTest, QueueMultipleResponses) {
    auto testClient = Square::Test::MakeTestSquareClient();

    Square::PaymentResult result1, result2;
    result1.paymentId = "pay_1";
    result1.status = "COMPLETED";
    result2.paymentId = "pay_2";
    result2.status = "PENDING";

    testClient->QueuePaymentResult(result1);
    testClient->QueuePaymentResult(result2);

    auto r1 = testClient->CreatePayment("n1", 100, "USD", "k1", "",
        "", Square::RetryPolicy::NoRetry());
    auto r2 = testClient->CreatePayment("n2", 200, "USD", "k2", "",
        "", Square::RetryPolicy::NoRetry());

    EXPECT_EQ(r1.paymentId, "pay_1");
    EXPECT_EQ(r2.paymentId, "pay_2");
}

TEST(TestSquareClientTest, QueuePaymentDeclinedError) {
    auto testClient = Square::Test::MakeTestSquareClient();
    testClient->QueuePaymentDeclined("INSUFFICIENT_FUNDS", "Not enough money");

    try {
        testClient->CreatePayment("nonce", 1000, "USD", "key", "",
            "", Square::RetryPolicy::NoRetry());
        FAIL() << "Expected PaymentDeclinedException";
    } catch (const Square::PaymentDeclinedException& e) {
        EXPECT_EQ(e.GetCode(), "INSUFFICIENT_FUNDS");
        EXPECT_THAT(e.GetDetail(), HasSubstr("enough money"));
    }
}

TEST(TestSquareClientTest, QueueInvalidRequestError) {
    auto testClient = Square::Test::MakeTestSquareClient();
    testClient->QueueInvalidRequest("BAD_VALUE", "Invalid amount", "amount_money");

    try {
        testClient->CreatePayment("nonce", -100, "USD", "key", "",
            "", Square::RetryPolicy::NoRetry());
        FAIL() << "Expected InvalidRequestException";
    } catch (const Square::InvalidRequestException& e) {
        EXPECT_EQ(e.GetCode(), "BAD_VALUE");
        EXPECT_EQ(e.GetField(), "amount_money");
    }
}

TEST(TestSquareClientTest, QueueRateLimitError) {
    auto testClient = Square::Test::MakeTestSquareClient();
    testClient->QueueRateLimitError();

    EXPECT_THROW({
        testClient->CreatePayment("nonce", 1000, "USD", "key", "",
            "", Square::RetryPolicy::NoRetry());
    }, Square::RateLimitException);
}

TEST(TestSquareClientTest, QueueServerError) {
    auto testClient = Square::Test::MakeTestSquareClient();
    testClient->QueueServerError("Internal failure");

    try {
        testClient->CreatePayment("nonce", 1000, "USD", "key", "",
            "", Square::RetryPolicy::NoRetry());
        FAIL() << "Expected SquareServerException";
    } catch (const Square::SquareServerException& e) {
        EXPECT_THAT(e.GetDetail(), HasSubstr("Internal failure"));
    }
}

TEST(TestSquareClientTest, SetNextPaymentResultClearsQueue) {
    auto testClient = Square::Test::MakeTestSquareClient();

    Square::PaymentResult result1, result2;
    result1.paymentId = "pay_1";
    result2.paymentId = "pay_2";

    testClient->QueuePaymentResult(result1);
    testClient->SetNextPaymentResult(result2);  // Should clear queue

    auto r = testClient->CreatePayment("n", 100, "USD", "k", "",
        "", Square::RetryPolicy::NoRetry());
    EXPECT_EQ(r.paymentId, "pay_2");

    // Second call should fail - queue is empty
    EXPECT_THROW({
        testClient->CreatePayment("n", 100, "USD", "k2", "",
            "", Square::RetryPolicy::NoRetry());
    }, std::runtime_error);
}

TEST(TestSquareClientTest, ClearCallsResetsHistory) {
    auto testClient = Square::Test::MakeTestSquareClient();

    Square::PaymentResult result;
    result.paymentId = "pay_test";
    testClient->QueuePaymentResult(result);
    testClient->QueuePaymentResult(result);

    testClient->CreatePayment("n1", 100, "USD", "k1", "",
        "", Square::RetryPolicy::NoRetry());
    EXPECT_EQ(testClient->GetCreatePaymentCallCount(), 1);

    testClient->ClearCalls();
    EXPECT_EQ(testClient->GetCreatePaymentCallCount(), 0);

    testClient->CreatePayment("n2", 200, "USD", "k2", "",
        "", Square::RetryPolicy::NoRetry());
    EXPECT_EQ(testClient->GetCreatePaymentCallCount(), 1);
    EXPECT_EQ(testClient->GetCreatePaymentCalls()[0].sourceId, "n2");
}

TEST(TestSquareClientTest, ThrowsWhenNoResponseConfigured) {
    auto testClient = Square::Test::MakeTestSquareClient();

    EXPECT_THROW({
        testClient->CreatePayment("nonce", 1000, "USD", "key", "",
            "", Square::RetryPolicy::NoRetry());
    }, std::runtime_error);
}

// CreateCustomer tests

TEST(TestSquareClientTest, CreateCustomerRecordsArgs) {
    auto testClient = Square::Test::MakeTestSquareClient();

    Square::CustomerResult result;
    result.customerId = "cust_test";
    testClient->QueueCustomerResult(result);

    auto r = testClient->CreateCustomer("idem-key", "test@example.com", "Test", "User");

    EXPECT_EQ(r.customerId, "cust_test");
    ASSERT_EQ(testClient->GetCreateCustomerCallCount(), 1);
    const auto& call = testClient->GetCreateCustomerCalls()[0];
    EXPECT_EQ(call.idempotencyKey, "idem-key");
    EXPECT_EQ(call.email, "test@example.com");
    EXPECT_EQ(call.firstName, "Test");
    EXPECT_EQ(call.lastName, "User");
}

TEST(TestSquareClientTest, GetCustomerRecordsArgs) {
    auto testClient = Square::Test::MakeTestSquareClient();

    Square::CustomerResult result;
    result.customerId = "cust_existing";
    testClient->QueueCustomerResult(result);

    auto r = testClient->GetCustomer("cust_existing");

    EXPECT_EQ(r.customerId, "cust_existing");
    ASSERT_EQ(testClient->GetGetCustomerCallCount(), 1);
    EXPECT_EQ(testClient->GetGetCustomerCalls()[0].customerId, "cust_existing");
}

TEST(TestSquareClientTest, CreateCustomerThrowsWhenNoResponse) {
    auto testClient = Square::Test::MakeTestSquareClient();

    EXPECT_THROW({
        testClient->CreateCustomer("key", "e@e.com", "F", "L");
    }, std::runtime_error);
}

// CreateCard tests

TEST(TestSquareClientTest, CreateCardRecordsArgs) {
    auto testClient = Square::Test::MakeTestSquareClient();

    Square::CreateCardResult result;
    result.cardId = "ccof:test_card";
    result.brand = "VISA";
    result.last4 = "4242";
    result.expMonth = 12;
    result.expYear = 2028;
    testClient->QueueCreateCardResult(result);

    auto r = testClient->CreateCard("idem-key", "cnon:nonce", "cust_123");

    EXPECT_EQ(r.cardId, "ccof:test_card");
    EXPECT_EQ(r.brand, "VISA");
    ASSERT_EQ(testClient->GetCreateCardCallCount(), 1);
    const auto& call = testClient->GetCreateCardCalls()[0];
    EXPECT_EQ(call.idempotencyKey, "idem-key");
    EXPECT_EQ(call.sourceId, "cnon:nonce");
    EXPECT_EQ(call.customerId, "cust_123");
}

TEST(TestSquareClientTest, CreateCardThrowsWhenNoResponse) {
    auto testClient = Square::Test::MakeTestSquareClient();

    EXPECT_THROW({
        testClient->CreateCard("key", "nonce", "cust");
    }, std::runtime_error);
}

// ListCards tests

TEST(TestSquareClientTest, ListCardsRecordsArgs) {
    auto testClient = Square::Test::MakeTestSquareClient();

    std::vector<Square::CardInfo> cards = {
        {"ccof:card1", "VISA", "1111", 3, 2027, true},
        {"ccof:card2", "MASTERCARD", "2222", 6, 2029, false}
    };
    testClient->QueueCardInfoList(cards);

    auto result = testClient->ListCards("cust_456");

    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].cardId, "ccof:card1");
    EXPECT_EQ(result[1].brand, "MASTERCARD");
    ASSERT_EQ(testClient->GetListCardsCallCount(), 1);
    EXPECT_EQ(testClient->GetListCardsCalls()[0].customerId, "cust_456");
}

TEST(TestSquareClientTest, ListCardsThrowsWhenNoResponse) {
    auto testClient = Square::Test::MakeTestSquareClient();

    EXPECT_THROW({
        testClient->ListCards("cust");
    }, std::runtime_error);
}

// DisableCard tests

TEST(TestSquareClientTest, DisableCardRecordsArgs) {
    auto testClient = Square::Test::MakeTestSquareClient();
    testClient->QueueDisableCardSuccess();

    testClient->DisableCard("ccof:card_to_disable");

    ASSERT_EQ(testClient->GetDisableCardCallCount(), 1);
    EXPECT_EQ(testClient->GetDisableCardCalls()[0].cardId, "ccof:card_to_disable");
}

TEST(TestSquareClientTest, DisableCardThrowsWhenNoResponse) {
    auto testClient = Square::Test::MakeTestSquareClient();

    EXPECT_THROW({
        testClient->DisableCard("ccof:card");
    }, std::runtime_error);
}

// ClearCalls clears all call histories

TEST(TestSquareClientTest, ClearCallsResetsAllHistories) {
    auto testClient = Square::Test::MakeTestSquareClient();

    Square::PaymentResult payResult;
    payResult.paymentId = "pay_1";
    testClient->QueuePaymentResult(payResult);
    testClient->CreatePayment("n", 100, "USD", "k", "", "",
        Square::RetryPolicy::NoRetry());

    Square::CustomerResult custResult;
    custResult.customerId = "cust_1";
    testClient->QueueCustomerResult(custResult);
    testClient->CreateCustomer("k", "e@e.com", "F", "L");

    testClient->ClearCalls();

    EXPECT_EQ(testClient->GetCreatePaymentCallCount(), 0);
    EXPECT_EQ(testClient->GetCreateCustomerCallCount(), 0);
    EXPECT_EQ(testClient->GetCreateCardCallCount(), 0);
    EXPECT_EQ(testClient->GetListCardsCallCount(), 0);
    EXPECT_EQ(testClient->GetDisableCardCallCount(), 0);
}

}  // namespace
