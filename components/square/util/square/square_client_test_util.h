#pragma once

#include "square_client.h"
#include "square_errors.h"

#include <exception>
#include <memory>
#include <queue>
#include <variant>
#include <vector>

namespace Square {
namespace Test {

// Captures arguments passed to CreatePayment for verification
struct CreatePaymentArgs {
    std::string sourceId;
    int64_t amountCents;
    std::string currency;
    std::string idempotencyKey;
    std::string note;
    std::string customerId;
    RetryPolicy retryPolicy;
};

struct CreateCustomerArgs {
    std::string idempotencyKey;
    std::string email;
    std::string firstName;
    std::string lastName;
};

struct CreateCardArgs {
    std::string idempotencyKey;
    std::string sourceId;
    std::string customerId;
};

struct GetCustomerArgs {
    std::string customerId;
};

struct ListCardsArgs {
    std::string customerId;
};

struct DisableCardArgs {
    std::string cardId;
};

struct RefundPaymentArgs {
    std::string paymentId;
    int64_t amountCents;
    std::string currency;
    std::string idempotencyKey;
    std::string reason;
    RetryPolicy retryPolicy;
};

class TestSquareClient : public SquareClient {
public:
    TestSquareClient() = default;
    TestSquareClient(const TestSquareClient&) = delete;
    TestSquareClient& operator=(const TestSquareClient&) = delete;
    ~TestSquareClient() override = default;

    PaymentResult CreatePayment(
        const std::string& sourceId,
        int64_t amountCents,
        const std::string& currency,
        const std::string& idempotencyKey,
        const std::string& note,
        const std::string& customerId,
        const RetryPolicy& retryPolicy) override;

    CustomerResult CreateCustomer(
        const std::string& idempotencyKey,
        const std::string& email,
        const std::string& firstName,
        const std::string& lastName) override;

    CustomerResult GetCustomer(const std::string& customerId) override;

    CreateCardResult CreateCard(
        const std::string& idempotencyKey,
        const std::string& sourceId,
        const std::string& customerId) override;

    std::vector<CardInfo> ListCards(const std::string& customerId) override;

    void DisableCard(const std::string& cardId) override;

    RefundResult RefundPayment(
        const std::string& paymentId,
        int64_t amountCents,
        const std::string& currency,
        const std::string& idempotencyKey,
        const std::string& reason,
        const RetryPolicy& retryPolicy) override;

    // Test verification helpers — Payments
    const std::vector<CreatePaymentArgs>& GetCreatePaymentCalls() const {
        return createPaymentCalls_;
    }
    size_t GetCreatePaymentCallCount() const {
        return createPaymentCalls_.size();
    }

    // Test verification helpers — Customers
    const std::vector<CreateCustomerArgs>& GetCreateCustomerCalls() const {
        return createCustomerCalls_;
    }
    size_t GetCreateCustomerCallCount() const {
        return createCustomerCalls_.size();
    }
    const std::vector<GetCustomerArgs>& GetGetCustomerCalls() const {
        return getCustomerCalls_;
    }
    size_t GetGetCustomerCallCount() const {
        return getCustomerCalls_.size();
    }

    // Test verification helpers — Cards
    const std::vector<CreateCardArgs>& GetCreateCardCalls() const {
        return createCardCalls_;
    }
    size_t GetCreateCardCallCount() const {
        return createCardCalls_.size();
    }
    const std::vector<ListCardsArgs>& GetListCardsCalls() const {
        return listCardsCalls_;
    }
    size_t GetListCardsCallCount() const {
        return listCardsCalls_.size();
    }
    const std::vector<DisableCardArgs>& GetDisableCardCalls() const {
        return disableCardCalls_;
    }
    size_t GetDisableCardCallCount() const {
        return disableCardCalls_.size();
    }

    // Test verification helpers — Refunds
    const std::vector<RefundPaymentArgs>& GetRefundPaymentCalls() const {
        return refundPaymentCalls_;
    }
    size_t GetRefundPaymentCallCount() const {
        return refundPaymentCalls_.size();
    }

    void ClearCalls();

    // Set up fake responses — Payments
    void QueuePaymentResult(PaymentResult result);
    void QueueError(std::exception_ptr error);

    // Convenience methods for common error types
    void QueuePaymentDeclined(
        const std::string& code = "CARD_DECLINED",
        const std::string& detail = "The card was declined");
    void QueueInvalidRequest(
        const std::string& code,
        const std::string& detail,
        const std::string& field = "");
    void QueueRateLimitError();
    void QueueServerError(const std::string& detail = "Internal server error");

    // Set next response (clears queue)
    void SetNextPaymentResult(PaymentResult result);

    // Set up fake responses — Customers
    void QueueCustomerResult(CustomerResult result);

    // Set up fake responses — Cards
    void QueueCreateCardResult(CreateCardResult result);
    void QueueCreateCardError(std::exception_ptr error);
    void QueueCardInfoList(std::vector<CardInfo> cards);
    void QueueDisableCardSuccess();
    void QueueDisableCardError(std::exception_ptr error);

    // Set up fake responses — Refunds
    void QueueRefundResult(RefundResult result);
    void QueueRefundError(std::exception_ptr error);

    void ClearQueuedResponses();

private:
    std::vector<CreatePaymentArgs> createPaymentCalls_;
    std::queue<std::variant<PaymentResult, std::exception_ptr>> queuedResponses_;

    std::vector<CreateCustomerArgs> createCustomerCalls_;
    std::vector<GetCustomerArgs> getCustomerCalls_;
    std::queue<std::variant<CustomerResult, std::exception_ptr>> customerResponses_;

    std::vector<CreateCardArgs> createCardCalls_;
    std::queue<std::variant<CreateCardResult, std::exception_ptr>> createCardResponses_;

    std::vector<ListCardsArgs> listCardsCalls_;
    std::queue<std::variant<std::vector<CardInfo>, std::exception_ptr>> listCardsResponses_;

    std::vector<DisableCardArgs> disableCardCalls_;
    std::queue<std::variant<int, std::exception_ptr>> disableCardResponses_;

    std::vector<RefundPaymentArgs> refundPaymentCalls_;
    std::queue<std::variant<RefundResult, std::exception_ptr>> refundResponses_;
};

using TestSquareClientPtr = std::shared_ptr<TestSquareClient>;

TestSquareClientPtr MakeTestSquareClient();

}  // namespace Test
}  // namespace Square
