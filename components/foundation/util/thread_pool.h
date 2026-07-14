#pragma once

#include <functional>
#include <memory>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>

class ThreadPool {
public:
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ~ThreadPool() = default;

    // Singleton accessors
    static ThreadPool& GetInstance();
    static void Shutdown();

    // Work submission and waiting
    void Queue(std::function<void()> fn);
    void Join();

private:
    ThreadPool(); 

    static std::unique_ptr<ThreadPool> s_instance;
    boost::asio::thread_pool pool_;
};