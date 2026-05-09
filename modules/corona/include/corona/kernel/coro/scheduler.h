#pragma once

#include <chrono>
#include <coroutine>
#include <memory>

#include "executor.h"
#include "task.h"
#include "tbb_executor.h"

namespace Corona::Kernel::Coro {

/**
 * @brief 全局协程调度器
 *
 * 管理协程的调度和执行。提供默认执行器和便捷的调度接口。
 *
 * 使用示例：
 * @code
 * // 使用默认调度器
 * co_await schedule_on_pool();
 *
 * // 自定义执行器
 * auto executor = std::make_shared<TbbExecutor>(4);
 * Scheduler::instance().set_default_executor(executor);
 * @endcode
 */
class Scheduler {
   public:
    /**
     * @brief 获取单例实例
     *
     * @return 调度器单例引用
     */
    static Scheduler& instance() {
        static Scheduler s;
        return s;
    }

    /**
     * @brief 设置默认执行器
     *
     * @param executor 执行器智能指针
     */
    void set_default_executor(std::shared_ptr<IExecutor> executor) {
        default_executor_ = std::move(executor);
    }

    /**
     * @brief 获取默认执行器
     *
     * 如果未设置，会惰性初始化一个 TbbExecutor
     *
     * @return 默认执行器引用
     */
    [[nodiscard]] IExecutor& default_executor() {
        if (!default_executor_) {
            // 惰性初始化 TBB 执行器
            default_executor_ = std::make_shared<TbbExecutor>();
        }
        return *default_executor_;
    }

    /**
     * @brief 检查是否有默认执行器
     *
     * @return 如果已设置或初始化默认执行器则返回 true
     */
    [[nodiscard]] bool has_default_executor() const noexcept {
        return default_executor_ != nullptr;
    }

    /**
     * @brief 调度协程到默认执行器
     *
     * @param handle 协程句柄
     */
    void schedule(std::coroutine_handle<> handle) {
        default_executor().execute([handle]() mutable { handle.resume(); });
    }

    /**
     * @brief 延迟调度协程
     *
     * @param handle 协程句柄
     * @param delay 延迟时间
     */
    void schedule_after(std::coroutine_handle<> handle, std::chrono::milliseconds delay) {
        default_executor().execute_after([handle]() mutable { handle.resume(); }, delay);
    }

    /**
     * @brief 重置调度器
     *
     * 清除默认执行器，主要用于测试
     */
    void reset() { default_executor_.reset(); }

   private:
    Scheduler() = default;
    ~Scheduler() = default;

    // 禁止拷贝和移动
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    std::shared_ptr<IExecutor> default_executor_;
};

// ========================================
// 调度相关 Awaitable
// ========================================

/**
 * @brief 切换到调度器管理的线程池
 *
 * 挂起当前协程，在默认执行器（TBB 线程池）上恢复执行。
 */
struct ScheduleOnPool {
    [[nodiscard]] bool await_ready() const noexcept { return false; }

    /**
     * @brief 调度到线程池
     *
     * @param handle 协程句柄
     * @return noop_coroutine 表示真正挂起，等待线程池异步恢复
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) const {
        Scheduler::instance().schedule(handle);
        return std::noop_coroutine();  // 真正挂起，等待线程池异步恢复
    }

    void await_resume() const noexcept {}
};

/**
 * @brief 便捷函数：切换到线程池执行
 *
 * @return ScheduleOnPool 等待器
 */
[[nodiscard]] inline ScheduleOnPool schedule_on_pool() { return ScheduleOnPool{}; }

/**
 * @brief 延迟后在线程池上恢复执行
 */
class ScheduleOnPoolAfter {
   public:
    explicit ScheduleOnPoolAfter(std::chrono::milliseconds delay) noexcept : delay_(delay) {}

    [[nodiscard]] bool await_ready() const noexcept { return delay_.count() <= 0; }

    /**
     * @brief 延迟后调度到线程池
     *
     * @param handle 协程句柄
     * @return noop_coroutine 表示真正挂起，等待延迟后异步恢复
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) const {
        Scheduler::instance().schedule_after(handle, delay_);
        return std::noop_coroutine();  // 真正挂起，等待延迟后异步恢复
    }

    void await_resume() const noexcept {}

   private:
    std::chrono::milliseconds delay_;
};

/**
 * @brief 便捷函数：延迟后在线程池上执行
 *
 * @param delay 延迟时间
 * @return ScheduleOnPoolAfter 等待器
 */
[[nodiscard]] inline ScheduleOnPoolAfter schedule_on_pool_after(
    std::chrono::milliseconds delay) {
    return ScheduleOnPoolAfter{delay};
}

// ========================================
// 协程运行器
// ========================================

/**
 * @brief 协程运行器
 *
 * 提供便捷的方法来运行和管理协程任务。
 */
class Runner {
   public:
    /**
     * @brief 阻塞运行一个任务直到完成
     *
     * @tparam T 任务返回类型
     * @param task 要运行的任务
     * @return 任务的返回值
     */
    template <typename T>
    static T run(Task<T> task) {
        return task.get();
    }

    /**
     * @brief 阻塞运行 void 任务
     *
     * @param task 要运行的任务
     */
    static void run(Task<void> task) { task.get(); }

    /**
     * @brief 启动任务到线程池（不等待完成）
     *
     * 注意：任务的生命周期由调用者管理，确保任务在完成前不被销毁
     *
     * @tparam T 任务返回类型
     * @param task 要启动的任务
     */
    template <typename T>
    static void spawn(Task<T>&& task) {
        // 将任务移动到堆上，在完成后自动清理
        auto* task_ptr = new Task<T>(std::move(task));
        Scheduler::instance().default_executor().execute([task_ptr]() {
            while (!task_ptr->done()) {
                task_ptr->resume();
            }
            delete task_ptr;
        });
    }
};

}  // namespace Corona::Kernel::Coro
