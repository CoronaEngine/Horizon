//
// Created by Zero on 2023/7/10.
//

#pragma once

#include <mutex>
#include <future>
#include <thread>
#include <memory>
#include <concepts>
#include <functional>
#include <condition_variable>

#include <core/stl.h>
#include "math/basic_types.h"
#include "core/header.h"

/// reference :https://github.com/LuisaGroup/LuisaCompute/src/core/concurrency/thread_pool.h
namespace ocarina {

class Barrier;

class OC_CORE_API ThreadPool {
private:
    vector<std::thread> threads_;
    queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    unique_ptr<Barrier> synchronize_barrier_;
    unique_ptr<Barrier> dispatch_barrier_;
    std::condition_variable cv_;
    std::atomic_uint task_count_;
    bool should_stop_;

private:
    void _dispatch(std::function<void()> task) noexcept;
    void _dispatch_all(std::function<void()> task, size_t max_threads = std::numeric_limits<size_t>::max()) noexcept;

public:
    explicit ThreadPool(size_t num_threads = 0u) noexcept;
    ~ThreadPool() noexcept;
    ThreadPool(ThreadPool &&) noexcept = delete;
    ThreadPool(const ThreadPool &) noexcept = delete;
    ThreadPool &operator=(ThreadPool &&) noexcept = delete;
    ThreadPool &operator=(const ThreadPool &) noexcept = delete;
    [[nodiscard]] static ThreadPool &instance() noexcept;
    [[nodiscard]] static uint worker_thread_index() noexcept;

public:
    void barrier() noexcept;
    void synchronize() noexcept;
    [[nodiscard]] auto size() const noexcept { return threads_.size(); }
    [[nodiscard]] uint task_count() const noexcept;

    template<typename F>
    requires std::is_invocable_v<F>
    auto async(F f) noexcept {
        using R = std::invoke_result_t<F>;
        auto promise = make_shared<std::promise<R>>(
            std::allocator_arg, ocarina::allocator{});
        auto future = promise->get_future().share();
        task_count_.fetch_add(1u);
        _dispatch([promise = std::move(promise), future, f = std::move(f), this]() mutable noexcept {
            if constexpr (std::same_as<R, void>) {
                f();
                promise->set_value();
            } else {
                promise->set_value(f());
            }
            task_count_.fetch_sub(1u);
        });
        return future;
    }

    template<typename F>
    requires std::is_invocable_v<F, uint>
    void parallel(uint n, F f) noexcept {
        if (n > 0u) {
            task_count_.fetch_add(1u);
            auto counter = make_shared<std::atomic_uint>(0u);
            _dispatch_all(
                [=, this]() mutable noexcept {
                    auto i = 0u;
                    while ((i = counter->fetch_add(1u)) < n) { f(i); }
                    if (i == n) { task_count_.fetch_sub(1u); }
                },
                n);
        }
    }

    template<typename F>
    requires std::is_invocable_v<F, uint, uint>
    void parallel(uint nx, uint ny, F f) noexcept {
        parallel(nx * ny, [=, f = std::move(f)](auto i) mutable noexcept {
            f(i % nx, i / nx);
        });
    }

    template<typename F>
    requires std::is_invocable_v<F, uint, uint, uint>
    void parallel(uint nx, uint ny, uint nz, F f) noexcept {
        parallel(nx * ny * nz, [=, f = std::move(f)](auto i) mutable noexcept {
            f(i % nx, i / nx % ny, i / nx / ny);
        });
    }

    template<typename... Args>
    void parallel_sync(Args &&...args) noexcept {
        parallel(OC_FORWARD(args)...);
        synchronize();
    }
};

template<typename F>
requires std::is_invocable_v<F>
inline auto async(F &&f) noexcept {
    return ThreadPool::instance().async(std::forward<F>(f));
}

template<typename... Args>
void parallel_for(Args &&...args) noexcept {
    ThreadPool::instance().parallel_sync(OC_FORWARD(args)...);
}

}// namespace ocarina