#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "sql_util/database_access/transaction_provider.h"

// A TransactionProvider backed by a bounded, lazily-grown pool of connections to
// ONE database. `maxConnections` defaults to 1 at the tenant layer (Model C
// launch), which reproduces ProductionTransactionProvider's
// single-connection-plus-mutex serialization exactly. Each RunInTransaction
// acquires a connection (creating one lazily up to the bound, otherwise waiting
// for a free one), runs the transaction on it, and returns it — so within a
// tenant work still serializes at the bound, while distinct tenants (distinct
// pools) never block one another.
//
// Implemented over ProductionTransactionProvider: each pooled slot is a
// ProductionTransactionProvider wrapping its own connection, so a pool of 1 is
// byte-for-byte the existing behavior; the pool only adds acquire/release +
// lazy growth on top.
class PooledTransactionProvider : public TransactionProvider {
public:
    PooledTransactionProvider(std::string databaseName, int maxConnections);
    ~PooledTransactionProvider() override;

    void RunInTransaction(TransactionProvider::DatabaseFunc databaseFunc) override;

    const std::string& GetDatabaseName() const { return databaseName_; }
    int GetMaxConnections() const { return maxConnections_; }
    // Connections created so far (<= maxConnections); 0 until the first transaction.
    int GetCreatedConnectionCount() const;
    // Metrics: how many acquisitions had to wait for a busy pool, and the total
    // wait time — so raising a tenant's bound becomes a data-driven decision.
    int64_t GetAcquireWaitCount() const;
    int64_t GetAcquireWaitMicros() const;

private:
    TransactionProviderPtr AcquireProvider();
    void ReleaseProvider(TransactionProviderPtr provider);

    std::string databaseName_;
    int maxConnections_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::vector<TransactionProviderPtr> freeProviders_;
    int createdCount_ = 0;
    int64_t acquireWaitCount_ = 0;
    int64_t acquireWaitMicros_ = 0;
};

TransactionProviderPtr MakePooledTransactionProvider(
    std::string databaseName, int maxConnections);

// Process-wide guardrail: the total connections created across ALL pools. When it
// crosses the soft ceiling a one-time warning is logged (the ~80-100
// max_connections instance limit — reach for PgBouncer or a lower per-tenant bound
// beyond it). A ceiling of 0 disables the check (the default until the app
// configures it).
void SetPooledConnectionSoftCeiling(int ceiling);
int GetTotalPooledConnections();
