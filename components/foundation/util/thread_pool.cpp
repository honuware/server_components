#include "thread_pool.h"

#include <utility>

std::unique_ptr<ThreadPool> ThreadPool::s_instance;

ThreadPool::ThreadPool()
    : pool_(8) {
}

ThreadPool& ThreadPool::GetInstance() {
    if (!s_instance) {
        s_instance = std::unique_ptr<ThreadPool>(new ThreadPool());
    }
    return *s_instance;
}

void ThreadPool::Shutdown() {
    if (s_instance) {
        s_instance->Join();
        s_instance.reset();
    }
}

void ThreadPool::Queue(std::function<void()> fn) {
    boost::asio::post(pool_, [f = std::move(fn)]() { f(); });
}

void ThreadPool::Join() {
    pool_.join();
}