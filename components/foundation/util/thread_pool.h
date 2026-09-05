#pragma once

#include <functional>
#include <memory>

// NOTE: this header deliberately does NOT include <boost/asio/...>.
//
// Crow uses STANDALONE Asio (the `asio` Conan package); Boost.Asio is a
// namespaced fork of the same code. From Boost 1.91 the two can no longer share
// a translation unit: both now define the same GLOBAL-namespace helper
// namespaces -- `asio_prefer_fn`, `asio_handler_cont_helpers` and friends -- so
// including <crow.h> and <boost/asio/thread_pool.hpp> together produces dozens of
// "redefinition" / "conflicts with a previous declaration" errors deep inside
// Asio, with nothing wrong in our own code.
//
// That collision is unavoidable in the endpoints layer, where crow is
// everywhere (it reaches even util/json_value.h and util/error_response.h) and
// ThreadPool is used for async writes. So the pool is hidden behind an opaque
// Impl and Boost.Asio stays confined to thread_pool.cpp, which never sees crow.
//
// Practical rule: keep this header Boost-free. Re-adding a <boost/asio/...>
// include here re-breaks every endpoint translation unit at once.
class ThreadPool {
public:
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    // Declared, not defaulted inline: Impl is incomplete here, and a defaulted
    // destructor in the header would need it complete to destroy the unique_ptr.
    ~ThreadPool();

    // Singleton accessors
    static ThreadPool& GetInstance();
    static void Shutdown();

    // Work submission and waiting
    void Queue(std::function<void()> fn);
    void Join();

private:
    ThreadPool();

    struct Impl;

    static std::unique_ptr<ThreadPool> s_instance;
    std::unique_ptr<Impl> impl_;
};
