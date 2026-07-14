#include "thread_pool.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace {

TEST(ThreadPoolTest, QueueBasic) {
    auto& pool = ThreadPool::GetInstance();

    std::atomic<int> count{0};
    pool.Queue([&] { count.fetch_add(1, std::memory_order_relaxed); });
    pool.Queue([&] { count.fetch_add(1, std::memory_order_relaxed); });

    pool.Join();
    EXPECT_EQ(count.load(std::memory_order_relaxed), 2);

    ThreadPool::Shutdown();
}

TEST(ThreadPoolTest, JoinBasic) {
    auto& pool = ThreadPool::GetInstance();

    constexpr int kTasks = 50;
    std::atomic<int> done{0};

    for (int i = 0; i < kTasks; ++i) {
        pool.Queue([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            done.fetch_add(1, std::memory_order_relaxed);
        });
    }

    pool.Join();
    EXPECT_EQ(done.load(std::memory_order_relaxed), kTasks);

    ThreadPool::Shutdown();
}

} // namespace