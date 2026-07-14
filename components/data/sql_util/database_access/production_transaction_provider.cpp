#include "production_transaction_provider.h"

#include "transaction_impl.h"

ProductionTransactionProvider::ProductionTransactionProvider(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

ProductionTransactionProvider::~ProductionTransactionProvider() {}

void ProductionTransactionProvider::RunInTransaction(TransactionProvider::DatabaseFunc databaseFunc) {
    // Lock for the FULL transaction lifetime — pqxx::work's ctor
    // opens the BEGIN against the connection, and the rest of the
    // body emits statements on it. Holding the lock across the
    // entire `pqxx::work` scope (ctor → user code → commit) is the
    // only way to guarantee the connection is single-threaded.
    // See the class-level comment in production_transaction_provider.h
    // for the broader rationale and the connection-pool follow-up.
    std::lock_guard<std::mutex> lock(connectionMutex_);
    pqxx::work trans(databaseHelper_.GetConnection(), "ProductionTransaction");
    TransactionImpl transaction(trans);
    databaseFunc(transaction);
    trans.commit();
}

TransactionProviderPtr MakeProductionTransactionProvider(
    DatabaseHelper databaseHelper) {
    return std::make_shared<ProductionTransactionProvider>(databaseHelper);
}
