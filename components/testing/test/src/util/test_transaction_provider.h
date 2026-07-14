#pragma once

#include "sql_util/database_access/transaction_provider.h"

class TestTransactionProvider : public TransactionProvider {
public:
    TestTransactionProvider(Transaction& transaction);
    TestTransactionProvider(const TestTransactionProvider&) = delete;
    TestTransactionProvider& operator=(const TestTransactionProvider&) = delete;
    ~TestTransactionProvider() override;

    void RunInTransaction(TransactionProvider::DatabaseFunc databaseFunc) override;

private:
    // This is a reference to a transaction created outside of this class that
    // must stay alive for the lifetime of this class.
    Transaction& transaction_;
};

TransactionProviderPtr MakeTestTransactionProvider(Transaction& transaction);