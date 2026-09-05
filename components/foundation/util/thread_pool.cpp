#include "thread_pool.h"

// Boost.Asio is confined to this translation unit on purpose -- see the note in
// thread_pool.h. This file never includes crow, so the two Asio copies never meet.
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>

#include <utility>

struct ThreadPool::Impl {
    Impl() : pool(8) {}

    boost::asio::thread_pool pool;
};

std::unique_ptr<ThreadPool> ThreadPool::s_instance;

ThreadPool::ThreadPool()
    : impl_(std::make_unique<Impl>()) {
}

// Impl is complete here, which is the whole reason this is not `= default` in
// the header.
ThreadPool::~ThreadPool() = default;

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
    boost::asio::post(impl_->pool, [f = std::move(fn)]() { f(); });
}

void ThreadPool::Join() {
    impl_->pool.join();
}
