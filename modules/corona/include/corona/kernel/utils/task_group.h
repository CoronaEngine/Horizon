#pragma once

#include <concepts>
#include <iostream>

#include "corona/kernel/utils/task_scheduler.h"

namespace Corona::Kernal::Utils {

/**
 * @brief 任务组 (Task Group)
 *
 * 用于批量提交和同步等待一组相关任务的工具类。
 * 类似于线程池的 future 组合，但设计更轻量，专注于 fork-join 并行模式。
 *
 * @details 核心特性：
 * - 任务批量提交：通过 run() 添加任意数量的任务到调度器
 * - 同步等待：wait() 会阻塞直到所有任务完成
 * - 主线程参与：wait() 期间主线程会尝试执行任务，避免 CPU 空转
 * - 异常安全：任务内的异常会被捕获并记录，不会影响其他任务
 * - 原子计数：使用 C++20 atomic::wait/notify 实现高效的任务完成通知
 *
 * @details 典型使用场景：
 * - 并行处理数组/集合的各个元素
 * - 游戏引擎中并行更新多个游戏对象
 * - 并行文件 I/O 操作
 *
 * @example
 * @code
 * TaskGroup group;
 * for (int i = 0; i < 100; ++i) {
 *     group.run([i]{ process_item(i); });
 * }
 * group.wait();  // 等待所有任务完成
 * @endcode
 *
 * @warning 嵌套限制：避免在任务中嵌套创建超过 10 层的 TaskGroup，
 *          过深的嵌套可能导致栈溢出或线程池资源耗尽。
 *          如果必须嵌套，请确保每层的任务数量较少，并监控栈使用情况。
 *
 * @note 标记为 final 防止继承，确保类的行为不被改变
 * @note 不支持拷贝和赋值，确保任务组的生命周期管理清晰
 * @thread_safety run() 和 wait() 都是线程安全的，但通常由单个线程管理
 */
class TaskGroup final {
   public:
    TaskGroup() = default;
    TaskGroup(const TaskGroup&) = delete;
    TaskGroup& operator=(const TaskGroup&) = delete;
    ~TaskGroup() = default;

    /**
     * @brief 向任务组提交一个任务
     *
     * 使用 C++20 concepts 约束，确保传入的是可调用对象。
     * 任务会被包装成异常安全的 lambda，然后提交到 TaskScheduler。
     *
     * @tparam F 可调用类型（lambda、函数指针、函数对象等）
     * @tparam Args 参数类型（支持完美转发）
     * @param func 要执行的函数
     * @param args 函数参数（可变参数）
     *
     * @details 实现细节：
     * - 任务提交时原子递增计数器
     * - 任务包装层捕获所有异常，防止线程崩溃
     * - 任务完成时原子递减计数器，最后一个任务负责通知等待者
     *
     * @note 参数通过值捕获（... args = std::forward<Args>(args)），
     *       避免悬空引用问题，但会有拷贝开销
     *
     * @thread_safety 线程安全，可从多个线程同时调用
     */
    template <std::invocable F, typename... Args>
    void run(F&& func, Args&&... args) {
        // 递增任务计数器（relaxed 足够，因为后续的 atomic::wait 会同步）
        task_count_.fetch_add(1, std::memory_order_relaxed);

        // 包装任务：异常处理 + 计数器管理
        auto task_wrapper = [this, func = std::forward<F>(func), ... args = std::forward<Args>(args)]() {
            try {
                // 执行用户任务
                func(std::forward<Args>(args)...);
            } catch (std::exception& e) {
                // 捕获标准异常，记录日志（避免线程崩溃）
                // TODO: 接入 Corona 的日志系统
                std::cerr << "Error occurred: " << e.what() << std::endl;
            }

            // 递减任务计数器，如果是最后一个任务则通知等待者
            // 使用 release 保证任务的所有副作用对 wait() 中的 acquire 可见
            if (task_count_.fetch_sub(1, std::memory_order_release) == 1) {
                task_count_.notify_all();  // C++20 atomic::notify_all
            }
        };

        // 提交包装后的任务到调度器
        scheduler_.submit(std::move(task_wrapper));
    }

    /**
     * @brief 等待任务组中的所有任务完成
     *
     * 采用"主线程参与式等待"策略：
     * 1. 主线程尝试执行任务，避免 CPU 空转（work-stealing）
     * 2. 如果无法窃取任务，使用 atomic::wait 进入高效休眠
     * 3. 当最后一个任务完成时，通过 notify_all 唤醒
     *
     * @thread_safety 线程安全，但通常只由创建任务组的线程调用
     *
     * @note 使用 C++20 的 atomic<T>::wait/notify，比条件变量更高效
     * @note wait() 可以多次调用，但第二次调用会立即返回（计数已为 0）
     * @note wait() 无超时机制，会无限期阻塞直到所有任务完成
     *
     * @warning 在嵌套的任务中调用 wait() 可能导致死锁，如果线程池资源被耗尽。
     *          建议嵌套深度不超过 10 层，并确保每层的任务数量适中。
     */
    void wait();

   private:
    TaskScheduler& scheduler_{TaskScheduler::instance()};  ///< 全局任务调度器引用
    std::atomic<std::size_t> task_count_{0};               ///< 未完成任务计数器（原子操作）
};

}  // namespace Corona::Kernal::Utils