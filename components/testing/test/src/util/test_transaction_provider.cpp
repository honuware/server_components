#include "test_transaction_provider.h"

#include "sql_util/database_access/transaction_impl.h"

TestTransactionProvider::TestTransactionProvider(Transaction& transaction)
    : transaction_(transaction) {
}

TestTransactionProvider::~TestTransactionProvider() {}

void TestTransactionProvider::RunInTransaction(TransactionProvider::DatabaseFunc databaseFunc) {
    // Note that this doesn't do anything because TestDatabaseUtil will own the transaction
    // and abort it so this is just passing along that transaction (which this class doesn't own).
    databaseFunc(transaction_);
}

TransactionProviderPtr MakeTestTransactionProvider(Transaction& transaction) {
    return std::make_shared<TestTransactionProvider>(transaction);
}