#pragma once

#include <atomic>
#include <coroutine>
#include <list>
#include <mutex>

#include "task.h"

namespace Corona::Kernel::Coro {

/**
 * @brief 异步互斥锁
 *
 * 协程友好的互斥锁。当锁被占用时，挂起当前协程而不是阻塞线程。
 * 适用于保护跨协程共享的数据结构。
 */
class AsyncMutex {
   public:
    AsyncMutex() = default;
    ~AsyncMutex() = default;
    AsyncMutex(const AsyncMutex&) = delete;
    AsyncMutex& operator=(const AsyncMutex&) = delete;

    /**
     * @brief 作用域锁
     */
    class ScopedLock {
       public:
        explicit ScopedLock(AsyncMutex& mutex) : mutex_(&mutex) {}
        ~ScopedLock() {
            if (mutex_) mutex_->unlock();
        }
        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;

        // 允许移动构造，以便从 Task 返回
        ScopedLock(ScopedLock&& other) noexcept : mutex_(other.mutex_) {
            other.mutex_ = nullptr;
        }

        ScopedLock& operator=(ScopedLock&& other) noexcept {
            if (this != &other) {
                if (mutex_) mutex_->unlock();
                mutex_ = other.mutex_;
                other.mutex_ = nullptr;
            }
            return *this;
        }

       private:
        AsyncMutex* mutex_;
    };

    /**
     * @brief 获取锁（Awaitable）
     *
     * @return Awaitable 对象，co_await 该对象将等待直到获得锁
     */
    [[nodiscard]] auto lock() { return LockAwaiter{*this}; }

    /**
     * @brief 获取锁并返回 ScopedLock
     *
     * 用法: auto guard = co_await mutex.lock_scoped();
     */
    [[nodiscard]] Task<ScopedLock> lock_scoped() {
        co_await lock();
        co_return ScopedLock{*this};
    }

    /**
     * @brief 释放锁
     *
     * 如果有等待的协程，将唤醒队列头部的协程。
     */
    void unlock() {
        std::coroutine_handle<> waiter = nullptr;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (waiters_.empty()) {
                locked_ = false;
            } else {
                waiter = waiters_.front();
                waiters_.pop_front();
                // 锁状态保持为 true，所有权转移给 waiter
            }
        }

        if (waiter) {
            waiter.resume();
        }
    }

   private:
    struct LockAwaiter {
        AsyncMutex& mutex;

        bool await_ready() const noexcept {
            std::lock_guard<std::mutex> guard(mutex.mutex_);
            if (!mutex.locked_) {
                mutex.locked_ = true;
                return true;
            }
            return false;
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept {
            std::lock_guard<std::mutex> guard(mutex.mutex_);
            // 双重检查，防止在 await_ready 和 await_suspend 之间锁被释放
            if (!mutex.locked_) {
                mutex.locked_ = true;
                return false;  // 不挂起，直接恢复
            }
            mutex.waiters_.push_back(h);
            return true;  // 挂起
        }

        void await_resume() const noexcept {}
    };

    // 使用 std::mutex 保护内部状态（等待队列和 locked 标志）
    // 因为临界区非常短（仅涉及 list 操作），不会造成长时间阻塞
    mutable std::mutex mutex_;
    std::list<std::coroutine_handle<>> waiters_;
    bool locked_ = false;
};

}  // namespace Corona::Kernel::Coro
