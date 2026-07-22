#include "pooled_transaction_provider.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "sql_util/database_access/transaction.h"
#include "test/src/util/database_test_helper.h"

namespace {

// The active test database (test_knottyyoga in the app suite, honuware_test in the
// standalone honuware suite). The pool opens its own real connections to it; the
// transactions run here are trivial, so nothing is written.
std::string ActiveTestDbName() {
    TestDatabaseUtil testDb;
    return testDb.GetDatabaseHelper().GetDatabaseName();
}

TEST(PooledTransactionProviderTest, LazyCreationAndReuse) {
    PooledTransactionProvider pool(ActiveTestDbName(), /*maxConnections=*/2);

    EXPECT_EQ(pool.GetCreatedConnectionCount(), 0);  // nothing created yet

    pool.RunInTransaction([](Transaction&) {});
    EXPECT_EQ(pool.GetCreatedConnectionCount(), 1);  // one created on first use

    pool.RunInTransaction([](Transaction&) {});
    EXPECT_EQ(pool.GetCreatedConnectionCount(), 1);  // reused, not grown
    EXPECT_EQ(pool.GetAcquireWaitCount(), 0);        // never contended
}

TEST(PooledTransactionProviderTest, PoolOfOneSerializes) {
    PooledTransactionProvider pool(ActiveTestDbName(), /*maxConnections=*/1);

    std::mutex m;
    std::condition_variable cv;
    bool firstInside = false;
    bool firstMayExit = false;
    std::atomic<bool> secondRan{false};

    std::thread first([&] {
        pool.RunInTransaction([&](Transaction&) {
            {
                std::lock_guard<std::mutex> lk(m);
                firstInside = true;
            }
            cv.notify_all();
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [&] { return firstMayExit; });
        });
    });

    // Wait until the first transaction holds the sole connection.
    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return firstInside; });
    }

    std::thread second([&] {
        pool.RunInTransaction([&](Transaction&) { secondRan = true; });
    });

    // The second acquisition must block (pool of 1). Poll the wait metric until it
    // registers the contention — bounded so a bug can't hang the suite.
    bool contended = false;
    for (int i = 0; i < 200 && !contended; ++i) {
        if (pool.GetAcquireWaitCount() >= 1) {
            contended = true;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    EXPECT_TRUE(contended);
    EXPECT_FALSE(secondRan.load());  // still blocked while the first holds it

    // Release the first; the second should then proceed.
    {
        std::lock_guard<std::mutex> lk(m);
        firstMayExit = true;
    }
    cv.notify_all();
    first.join();
    second.join();

    EXPECT_TRUE(secondRan.load());
    EXPECT_EQ(pool.GetCreatedConnectionCount(), 1);  // never exceeded the bound
    EXPECT_GE(pool.GetAcquireWaitCount(), 1);
}

TEST(PooledTransactionProviderTest, PoolOfTwoOverlaps) {
    PooledTransactionProvider pool(ActiveTestDbName(), /*maxConnections=*/2);

    std::mutex m;
    std::condition_variable cv;
    int arrived = 0;
    // Returns true only if BOTH transactions are inside at the same time — which
    // can only happen if the pool ran them concurrently.
    auto bothInside = [&] {
        std::unique_lock<std::mutex> lk(m);
        ++arrived;
        cv.notify_all();
        return cv.wait_for(lk, std::chrono::seconds(10), [&] { return arrived >= 2; });
    };

    auto run = [&] {
        bool overlapped = false;
        pool.RunInTransaction([&](Transaction&) { overlapped = bothInside(); });
        return overlapped;
    };

    std::future<bool> a = std::async(std::launch::async, run);
    std::future<bool> b = std::async(std::launch::async, run);

    EXPECT_TRUE(a.get());  // both reached the barrier together → genuine overlap
    EXPECT_TRUE(b.get());
    EXPECT_EQ(pool.GetCreatedConnectionCount(), 2);
    EXPECT_EQ(pool.GetAcquireWaitCount(), 0);  // two connections, no contention
}

TEST(PooledTransactionProviderTest, ConnectionReturnedAfterException) {
    PooledTransactionProvider pool(ActiveTestDbName(), /*maxConnections=*/1);

    EXPECT_ANY_THROW(pool.RunInTransaction(
        [](Transaction&) { throw std::runtime_error("boom"); }));

    // The sole connection must have been returned, so the next call succeeds and
    // the pool did not grow past its bound.
    bool ran = false;
    pool.RunInTransaction([&](Transaction&) { ran = true; });
    EXPECT_TRUE(ran);
    EXPECT_EQ(pool.GetCreatedConnectionCount(), 1);
}

TEST(PooledTransactionProviderTest, TotalConnectionCounterTracksLifetime) {
    const int before = GetTotalPooledConnections();
    {
        PooledTransactionProvider pool(ActiveTestDbName(), /*maxConnections=*/1);
        pool.RunInTransaction([](Transaction&) {});
        EXPECT_EQ(GetTotalPooledConnections(), before + 1);
    }
    // Destroying the pool releases its connections from the aggregate count.
    EXPECT_EQ(GetTotalPooledConnections(), before);
}

}  // namespace
