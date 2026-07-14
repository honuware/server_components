#pragma once

#include "transaction.h"

class TransactionProvider {
public:
    TransactionProvider(const TransactionProvider&) = delete;
    TransactionProvider& operator=(const TransactionProvider&) = delete;
    virtual ~TransactionProvider() = default;

    using DatabaseFunc = std::function<void(Transaction&)>;

    virtual void RunInTransaction(DatabaseFunc databaseFunc) = 0;

protected:
    TransactionProvider() = default;
};

using TransactionProviderPtr = std::shared_ptr<TransactionProvider>;

