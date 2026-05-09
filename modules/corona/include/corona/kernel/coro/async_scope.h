#pragma once

#include <atomic>
#include <coroutine>
#include <exception>
#include <vector>

#include "awaitables.h"
#include "task.h"

namespace Corona::Kernel::Coro {

/**
 * @brief 异步作用域
 *
 * 用于管理一组并发任务的生命周期。允许 "fire-and-forget" 式地启动任务，
 * 并提供 join() 方法等待所有任务完成。
 *
 * 实现了结构化并发的基本语义：确保作用域销毁前所有任务都已结束。
 */
class AsyncScope {
   public:
    AsyncScope() : cv_(std::make_shared<ConditionVariable>()) {}

    // 禁止拷贝和移动，因为内部任务可能持有指向 Scope 的指针
    AsyncScope(const AsyncScope&) = delete;
    AsyncScope& operator=(const AsyncScope&) = delete;

    /**
     * @brief 启动一个新任务
     *
     * 任务会立即开始执行（Fire-and-forget）。
     * 任务的生命周期由 Scope 管理。
     *
     * @param task 要启动的任务
     */
    template <typename T>
    void spawn(Task<T>&& task) {
        active_count_.fetch_add(1, std::memory_order_relaxed);
        // 启动 FireAndForget 协程包装器
        run_task(std::move(task));
    }

    /**
     * @brief 等待所有任务完成
     *
     * 挂起当前协程，直到作用域内所有任务都执行完毕。
     */
    [[nodiscard]] auto join() {
        // 使用 P0 实现的 Async ConditionVariable
        return cv_->wait([this] { return active_count_.load(std::memory_order_acquire) == 0; });
    }

   private:
    // 内部使用的 Fire-and-forget 协程类型
    struct FireAndForget {
        struct promise_type {
            FireAndForget get_return_object() { return {}; }
            std::suspend_never initial_suspend() { return {}; }
            std::suspend_never final_suspend() noexcept { return {}; }
            void return_void() {}
            void unhandled_exception() {
                // TODO: 考虑将异常传播到 AsyncScope
                // 目前仅忽略，避免 crash
            }
        };
    };

    template <typename T>
    FireAndForget run_task(Task<T> task) {
        try {
            co_await task;
        } catch (...) {
            // 异常被吞没或终止（取决于 promise_type）
            // 改进方向：使用 std::vector<std::exception_ptr> 收集异常
        }

        if (active_count_.fetch_sub(1, std::memory_order_release) == 1) {
            cv_->notify_all();
        }
    }

    std::atomic<size_t> active_count_{0};
    std::shared_ptr<ConditionVariable> cv_;
};

}  // namespace Corona::Kernel::Coro
