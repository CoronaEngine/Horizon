#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <optional>

namespace Corona::Kernal::Utils {

/// 任务类型定义：无参数、无返回值的可调用对象
using Task = std::function<void()>;

/**
 * @brief 工作窃取队列 (Work-Stealing Queue)
 *
 * 用于多线程任务调度的双端队列实现。支持从队列头部弹出任务（所有者线程），
 * 以及从队列尾部窃取任务（其他线程）。这种设计可以有效平衡多线程负载。
 *
 * @details 工作窃取算法原理：
 * - 每个工作线程拥有自己的任务队列
 * - 线程优先从自己队列的头部取任务（FIFO）
 * - 当自己的队列为空时，随机选择其他线程的队列，从尾部窃取任务
 * - 头部和尾部操作减少了锁竞争的概率
 *
 * @note 标记为 final 防止继承，确保类的行为不被改变
 * @thread_safety 所有公共方法都是线程安全的
 */
class WorkStealingQueue final {
   public:
    WorkStealingQueue() = default;
    ~WorkStealingQueue() = default;

    /**
     * @brief 将任务推入队列尾部
     *
     * 通常由队列的所有者线程调用，将新任务添加到队列。
     *
     * @param task 要添加的任务（移动语义）
     * @thread_safety 线程安全，内部使用互斥锁保护
     */
    void push(Task task);

    /**
     * @brief 尝试从队列头部弹出任务（所有者操作）
     *
     * 由队列的所有者线程调用，从队列头部获取任务。
     * 采用 FIFO 顺序，优先执行最早添加的任务。
     *
     * @return 如果队列非空，返回队列头部的任务；否则返回 std::nullopt
     * @thread_safety 线程安全，内部使用互斥锁保护
     */
    [[nodiscard]] std::optional<Task> try_pop();

    /**
     * @brief 尝试从队列尾部窃取任务（窃取者操作）
     *
     * 由其他工作线程调用，从队列尾部获取任务。
     * 采用 LIFO 顺序，从尾部窃取最近添加的任务，减少与 try_pop 的竞争。
     *
     * @return 如果队列非空，返回队列尾部的任务；否则返回 std::nullopt
     * @thread_safety 线程安全，内部使用互斥锁保护
     */
    [[nodiscard]] std::optional<Task> try_steal();

   private:
    std::deque<Task> tasks_;  ///< 任务双端队列，支持高效的头尾操作
    std::mutex mutex_;        ///< 互斥锁，保护任务队列的并发访问
};

}  // namespace Corona::Kernal::Utils