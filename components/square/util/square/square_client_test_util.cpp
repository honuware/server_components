#include "square_client_test_util.h"

#include <stdexcept>

namespace Square {
namespace Test {

PaymentResult TestSquareClient::CreatePayment(
    const std::string& sourceId,
    int64_t amountCents,
    const std::string& currency,
    const std::string& idempotencyKey,
    const std::string& note,
    const std::string& customerId,
    const RetryPolicy& retryPolicy) {

    createPaymentCalls_.push_back({
        sourceId, amountCents, currency, idempotencyKey, note, customerId, retryPolicy
    });

    if (queuedResponses_.empty()) {
        throw std::runtime_error(
            "TestSquareClient: No response configured. "
            "Call QueuePaymentResult or SetNextPaymentResult before CreatePayment.");
    }

    auto queued = std::move(queuedResponses_.front());
    queuedResponses_.pop();

    if (std::holds_alternative<std::exception_ptr>(queued)) {
        std::rethrow_exception(std::get<std::exception_ptr>(queued));
    }

    return std::get<PaymentResult>(queued);
}

CustomerResult TestSquareClient::CreateCustomer(
    const std::string& idempotencyKey,
    const std::string& email,
    const std::string& firstName,
    const std::string& lastName) {

    createCustomerCalls_.push_back({idempotencyKey, email, firstName, lastName});

    if (customerResponses_.empty()) {
        throw std::runtime_error(
            "TestSquareClient: No customer response configured. "
            "Call QueueCustomerResult before CreateCustomer.");
    }

    auto queued = std::move(customerResponses_.front());
    customerResponses_.pop();

    if (std::holds_alternative<std::exception_ptr>(queued)) {
        std::rethrow_exception(std::get<std::exception_ptr>(queued));
    }

    return std::get<CustomerResult>(queued);
}

CustomerResult TestSquareClient::GetCustomer(const std::string& customerId) {
    getCustomerCalls_.push_back({customerId});

    if (customerResponses_.empty()) {
        throw std::runtime_error(
            "TestSquareClient: No customer response configured. "
            "Call QueueCustomerResult before GetCustomer.");
    }

    auto queued = std::move(customerResponses_.front());
    customerResponses_.pop();

    if (std::holds_alternative<std::exception_ptr>(queued)) {
        std::rethrow_exception(std::get<std::exception_ptr>(queued));
    }

    return std::get<CustomerResult>(queued);
}

CreateCardResult TestSquareClient::CreateCard(
    const std::string& idempotencyKey,
    const std::string& sourceId,
    const std::string& customerId) {

    createCardCalls_.push_back({idempotencyKey, sourceId, customerId});

    if (createCardResponses_.empty()) {
        throw std::runtime_error(
            "TestSquareClient: No card response configured. "
            "Call QueueCreateCardResult before CreateCard.");
    }

    auto queued = std::move(createCardResponses_.front());
    createCardResponses_.pop();

    if (std::holds_alternative<std::exception_ptr>(queued)) {
        std::rethrow_exception(std::get<std::exception_ptr>(queued));
    }

    return std::get<CreateCardResult>(queued);
}

std::vector<CardInfo> TestSquareClient::ListCards(const std::string& customerId) {
    listCardsCalls_.push_back({customerId});

    if (listCardsResponses_.empty()) {
        throw std::runtime_error(
            "TestSquareClient: No list cards response configured. "
            "Call QueueCardInfoList before ListCards.");
    }

    auto queued = std::move(listCardsResponses_.front());
    listCardsResponses_.pop();

    if (std::holds_alternative<std::exception_ptr>(queued)) {
        std::rethrow_exception(std::get<std::exception_ptr>(queued));
    }

    return std::get<std::vector<CardInfo>>(queued);
}

void TestSquareClient::DisableCard(const std::string& cardId) {
    disableCardCalls_.push_back({cardId});

    if (disableCardResponses_.empty()) {
        throw std::runtime_error(
            "TestSquareClient: No disable card response configured. "
            "Call QueueError or set up a response before DisableCard.");
    }

    auto queued = std::move(disableCardResponses_.front());
    disableCardResponses_.pop();

    if (std::holds_alternative<std::exception_ptr>(queued)) {
        std::rethrow_exception(std::get<std::exception_ptr>(queued));
    }
}

void TestSquareClient::ClearCalls() {
    createPaymentCalls_.clear();
    createCustomerCalls_.clear();
    getCustomerCalls_.clear();
    createCardCalls_.clear();
    listCardsCalls_.clear();
    disableCardCalls_.clear();
    refundPaymentCalls_.clear();
}

RefundResult TestSquareClient::RefundPayment(
    const std::string& paymentId,
    int64_t amountCents,
    const std::string& currency,
    const std::string& idempotencyKey,
    const std::string& reason,
    const RetryPolicy& retryPolicy) {

    refundPaymentCalls_.push_back({
        paymentId, amountCents, currency, idempotencyKey, reason, retryPolicy
    });

    if (refundResponses_.empty()) {
        throw std::runtime_error(
            "TestSquareClient: No refund response configured. "
            "Call QueueRefundResult before RefundPayment.");
    }

    auto queued = std::move(refundResponses_.front());
    refundResponses_.pop();

    if (std::holds_alternative<std::exception_ptr>(queued)) {
        std::rethrow_exception(std::get<std::exception_ptr>(queued));
    }

    return std::get<RefundResult>(queued);
}

void TestSquareClient::QueuePaymentResult(PaymentResult result) {
    queuedResponses_.push(std::move(result));
}

void TestSquareClient::QueueError(std::exception_ptr error) {
    queuedResponses_.push(error);
}

void TestSquareClient::QueuePaymentDeclined(
    const std::string& code,
    const std::string& detail) {
    try {
        throw PaymentDeclinedException("PAYMENT_METHOD_ERROR", code, detail);
    } catch (...) {
        queuedResponses_.push(std::current_exception());
    }
}

void TestSquareClient::QueueInvalidRequest(
    const std::string& code,
    const std::string& detail,
    const std::string& field) {
    try {
        throw InvalidRequestException(code, detail, field);
    } catch (...) {
        queuedResponses_.push(std::current_exception());
    }
}

void TestSquareClient::QueueRateLimitError() {
    try {
        throw RateLimitException("Rate limit exceeded");
    } catch (...) {
        queuedResponses_.push(std::current_exception());
    }
}

void TestSquareClient::QueueServerError(const std::string& detail) {
    try {
        throw SquareServerException("INTERNAL_ERROR", detail);
    } catch (...) {
        queuedResponses_.push(std::current_exception());
    }
}

void TestSquareClient::SetNextPaymentResult(PaymentResult result) {
    ClearQueuedResponses();
    QueuePaymentResult(std::move(result));
}

void TestSquareClient::QueueCustomerResult(CustomerResult result) {
    customerResponses_.push(std::move(result));
}

void TestSquareClient::QueueCreateCardResult(CreateCardResult result) {
    createCardResponses_.push(std::move(result));
}

void TestSquareClient::QueueCardInfoList(std::vector<CardInfo> cards) {
    listCardsResponses_.push(std::move(cards));
}

void TestSquareClient::QueueCreateCardError(std::exception_ptr error) {
    createCardResponses_.push(error);
}

void TestSquareClient::QueueDisableCardError(std::exception_ptr error) {
    disableCardResponses_.push(error);
}

void TestSquareClient::QueueDisableCardSuccess() {
    disableCardResponses_.push(0);
}

void TestSquareClient::QueueRefundResult(RefundResult result) {
    refundResponses_.push(std::move(result));
}

void TestSquareClient::QueueRefundError(std::exception_ptr error) {
    refundResponses_.push(error);
}

void TestSquareClient::ClearQueuedResponses() {
    std::queue<std::variant<PaymentResult, std::exception_ptr>> emptyPayments;
    queuedResponses_.swap(emptyPayments);

    std::queue<std::variant<CustomerResult, std::exception_ptr>> emptyCustomers;
    customerResponses_.swap(emptyCustomers);

    std::queue<std::variant<CreateCardResult, std::exception_ptr>> emptyCards;
    createCardResponses_.swap(emptyCards);

    std::queue<std::variant<std::vector<CardInfo>, std::exception_ptr>> emptyListCards;
    listCardsResponses_.swap(emptyListCards);

    std::queue<std::variant<int, std::exception_ptr>> emptyDisableCards;
    disableCardResponses_.swap(emptyDisableCards);

    std::queue<std::variant<RefundResult, std::exception_ptr>> emptyRefunds;
    refundResponses_.swap(emptyRefunds);
}

TestSquareClientPtr MakeTestSquareClient() {
    return std::make_shared<TestSquareClient>();
}

}  // namespace Test
}  // namespace Square
