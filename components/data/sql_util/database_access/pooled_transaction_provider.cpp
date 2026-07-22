#include "pooled_transaction_provider.h"

#include <atomic>
#include <chrono>
#include <utility>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/production_transaction_provider.h"
#include "util/logging.h"

namespace {

// Aggregate connections created across every pool in the process, with a soft
// ceiling that warns once when crossed (guardrail for the instance-wide
// max_connections limit).
std::atomic<int> g_totalPooledConnections{0};
std::atomic<int> g_softCeiling{0};  // 0 = disabled
std::atomic<bool> g_ceilingWarned{false};

void OnConnectionCreated() {
    const int total = g_totalPooledConnections.fetch_add(1) + 1;
    const int ceiling = g_softCeiling.load();
    if (ceiling > 0 && total >= ceiling && !g_ceilingWarned.exchange(true)) {
        LogInfo()
            << "PooledTransactionProvider: aggregate pooled connections (" << total
            << ") reached the soft ceiling (" << ceiling
            << "). Approaching the Postgres max_connections limit — consider "
               "PgBouncer or a lower per-tenant max_connections.\n";
    }
}

void OnConnectionsDestroyed(int count) {
    if (count > 0) g_totalPooledConnections.fetch_sub(count);
}

}  // namespace

void SetPooledConnectionSoftCeiling(int ceiling) {
    g_softCeiling.store(ceiling);
    g_ceilingWarned.store(false);  // let the warning fire again after a reconfigure
}

int GetTotalPooledConnections() {
    return g_totalPooledConnections.load();
}

PooledTransactionProvider::PooledTransactionProvider(
    std::string databaseName, int maxConnections)
    : databaseName_(std::move(databaseName)),
      maxConnections_(maxConnections > 0 ? maxConnections : 1) {}

PooledTransactionProvider::~PooledTransactionProvider() {
    OnConnectionsDestroyed(createdCount_);
}

TransactionProviderPtr PooledTransactionProvider::AcquireProvider() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        if (!freeProviders_.empty()) {
            TransactionProviderPtr provider = std::move(freeProviders_.back());
            freeProviders_.pop_back();
            return provider;
        }
        if (createdCount_ < maxConnections_) {
            // Grow the pool lazily, up to the bound. Done under the lock so two
            // racing acquisitions can't over-create; connection setup is a
            // one-time per-slot cost.
            DatabaseHelper databaseHelper =
                MakeProductionDatabaseHelper(databaseName_);
            TransactionProviderPtr provider =
                MakeProductionTransactionProvider(databaseHelper);
            ++createdCount_;
            OnConnectionCreated();
            return provider;
        }
        // Pool exhausted — wait for a released connection.
        ++acquireWaitCount_;
        const auto waitStart = std::chrono::steady_clock::now();
        available_.wait(lock, [&] { return !freeProviders_.empty(); });
        acquireWaitMicros_ += std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::steady_clock::now() - waitStart)
                                  .count();
    }
}

void PooledTransactionProvider::ReleaseProvider(TransactionProviderPtr provider) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        freeProviders_.push_back(std::move(provider));
    }
    available_.notify_one();
}

void PooledTransactionProvider::RunInTransaction(
    TransactionProvider::DatabaseFunc databaseFunc) {
    TransactionProviderPtr provider = AcquireProvider();
    try {
        provider->RunInTransaction(std::move(databaseFunc));
    } catch (...) {
        ReleaseProvider(std::move(provider));  // return the connection on failure
        throw;
    }
    ReleaseProvider(std::move(provider));
}

int PooledTransactionProvider::GetCreatedConnectionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return createdCount_;
}

int64_t PooledTransactionProvider::GetAcquireWaitCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return acquireWaitCount_;
}

int64_t PooledTransactionProvider::GetAcquireWaitMicros() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return acquireWaitMicros_;
}

TransactionProviderPtr MakePooledTransactionProvider(
    std::string databaseName, int maxConnections) {
    return std::make_shared<PooledTransactionProvider>(
        std::move(databaseName), maxConnections);
}
