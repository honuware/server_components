#include "production_transaction_provider.h"

#include <memory>

#include "transaction_impl.h"
#include "util/logging.h"

ProductionTransactionProvider::ProductionTransactionProvider(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

ProductionTransactionProvider::~ProductionTransactionProvider() {}

void ProductionTransactionProvider::Reconnect(std::string_view reason) {
    try {
        databaseHelper_.ResetConnection();
    } catch (const std::exception& e) {
        LogError() << "[database] event=reconnect_failed database="
                   << databaseHelper_.GetDatabaseName() << " reason=" << reason
                   << " error=\"" << e.what() << "\"\n";
        throw;
    }
    LogWarning() << "[database] event=reconnected database="
                 << databaseHelper_.GetDatabaseName() << " reason=" << reason << "\n";
}

void ProductionTransactionProvider::RunInTransaction(TransactionProvider::DatabaseFunc databaseFunc) {
    // Lock for the FULL transaction lifetime — pqxx::work's ctor
    // opens the BEGIN against the connection, and the rest of the
    // body emits statements on it. Holding the lock across the
    // entire `pqxx::work` scope (ctor → user code → commit) is the
    // only way to guarantee the connection is single-threaded.
    // See the class-level comment in production_transaction_provider.h
    // for the broader rationale and the connection-pool follow-up.
    std::lock_guard<std::mutex> lock(connectionMutex_);

    // A connection libpq already knows is dead (an earlier transaction lost
    // it) is replaced before anything is sent on it.
    if (!databaseHelper_.GetConnection().is_open()) {
        Reconnect("connection_closed");
    }

    std::unique_ptr<pqxx::work> trans;
    try {
        trans = std::make_unique<pqxx::work>(
            databaseHelper_.GetConnection(), "ProductionTransaction");
    } catch (const std::exception&) {
        // BEGIN failed. If the connection is still open this is a genuine
        // error, and it propagates. If it is closed, the connection died while
        // idle -- the 10/15/2026 case: RDS hung up hours earlier and nothing
        // had used the socket since. None of the caller's work has run yet, so
        // one fresh connection and one more BEGIN is safe.
        if (databaseHelper_.GetConnection().is_open()) {
            throw;
        }
        Reconnect("begin_failed");
        trans = std::make_unique<pqxx::work>(
            databaseHelper_.GetConnection(), "ProductionTransaction");
    }

    // From here on the caller's work is NEVER retried, because it may already
    // have done something outside the database (a Square charge, an email). If
    // the connection breaks mid-way the exception propagates, and the NEXT
    // transaction finds the connection closed and reconnects above.
    TransactionImpl transaction(*trans);
    databaseFunc(transaction);
    trans->commit();
}

TransactionProviderPtr MakeProductionTransactionProvider(
    DatabaseHelper databaseHelper) {
    return std::make_shared<ProductionTransactionProvider>(databaseHelper);
}
