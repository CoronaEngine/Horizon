#pragma once

#include <corona/kernel/utils/work_stealing_queue.h>

namespace Corona::Kernal::Utils {

/**
 * @brief 任务调度器 (Task Scheduler)
 *
 * 基于工作窃取算法的多线程任务调度器，采用单例模式。
 * 自动创建与 CPU 核心数相同的工作线程，每个线程维护独立的任务队列。
 *
 * @details 核心特性：
 * - 工作窃取 (Work-Stealing)：空闲线程可以从其他线程的队列中窃取任务
 * - 负载均衡：任务提交采用轮询分配，运行时通过窃取实现动态平衡
 * - 线程休眠：使用条件变量，空闲时休眠节省 CPU 资源
 * - 优雅关闭：使用 C++20 stop_token 实现协作式取消
 *
 * @details 使用场景：
 * - 并行计算密集型任务
 * - 游戏引擎的并行更新系统
 * - 异步 I/O 操作的后台处理
 *
 * @example
 * @code
 * auto& scheduler = TaskScheduler::instance();
 * scheduler.submit([]{ std::cout << "Task executed\n"; });
 * @endcode
 *
 * @note 单例实例在首次调用 instance() 时创建，程序结束时自动销毁
 * @thread_safety 所有公共方法都是线程安全的
 */
class TaskScheduler final {
   public:
    /**
     * @brief 获取任务调度器的单例实例
     *
     * @return 调度器的全局唯一实例引用
     * @thread_safety 线程安全（C++11 保证静态局部变量的线程安全初始化）
     */
    static TaskScheduler& instance();

    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    /**
     * @brief 提交任务到调度器
     *
     * 任务会被分配到某个工作线程的队列中，采用轮询策略实现负载均衡。
     * 提交后会唤醒一个休眠的工作线程（如果有）。
     *
     * @param task 要执行的任务（移动语义，避免拷贝）
     * @thread_safety 线程安全，可从任意线程调用
     *
     * @note 任务会被异步执行，不保证立即执行
     * @note 任务内部的异常需要自行处理，否则会导致程序终止
     */
    void submit(Task task);

    /**
     * @brief 尝试执行一个任务（同步调用）
     *
     * 调用线程会尝试从所有工作队列中窃取并执行一个任务。
     * 常用于主线程参与任务执行，避免空转等待。
     *
     * @return 如果成功窃取并执行了任务返回 true，否则返回 false
     * @thread_safety 线程安全，可从任意线程调用
     *
     * @note 此方法会阻塞直到任务执行完成
     * @see TaskGroup::wait() 使用此方法实现主线程参与式等待
     */
    bool try_execute_task();

   private:
    /**
     * @brief 私有构造函数（单例模式）
     *
     * 根据 CPU 核心数创建工作线程，每个线程运行 worker_loop。
     */
    TaskScheduler();

    /**
     * @brief 析构函数
     *
     * 请求所有工作线程停止，并唤醒休眠线程，等待所有线程退出。
     */
    ~TaskScheduler();

    /**
     * @brief 工作线程的主循环
     *
     * 每个线程的执行逻辑：
     * 1. 尝试从自己的队列中取任务
     * 2. 如果自己队列为空，尝试从其他线程窃取任务
     * 3. 如果窃取失败，进入休眠等待新任务或停止信号
     *
     * @param index 当前线程的索引（对应 task_queues_ 的索引）
     * @param stop_token 停止令牌，用于协作式取消
     */
    void worker_loop(std::uint32_t index, std::stop_token stop_token);

    /**
     * @brief 尝试从其他线程窃取并执行任务
     *
     * 随机选择一个起始队列，然后遍历所有其他线程的队列尝试窃取。
     * 随机起始位置避免所有线程总是从同一个队列窃取。
     *
     * @param index 调用线程的索引（可选）。如果提供，会跳过自己的队列
     * @return 如果成功窃取并执行了任务返回 true，否则返回 false
     */
    bool try_steal_and_execute(std::optional<std::uint32_t> index = std::nullopt);

    /**
     * @brief 检查是否还有任务待执行
     *
     * @return 当前实现简化为始终返回 true
     * @todo 未来可以优化为实际检查所有队列是否为空
     */
    bool has_any_task() const;

   private:
    const uint32_t thread_count_;                 ///< 工作线程数量（等于 CPU 核心数）
    std::vector<std::jthread> threads_;           ///< 工作线程池（C++20 jthread 自动 join）
    std::vector<WorkStealingQueue> task_queues_;  ///< 每个线程对应一个任务队列
    std::atomic<uint32_t> submission_index_{0};   ///< 任务提交轮询计数器（原子操作保证线程安全）

    std::stop_source stop_source_;  ///< 停止源，用于请求所有线程停止

    // 线程休眠与唤醒机制
    std::condition_variable cv_;  ///< 条件变量，用于线程休眠和唤醒
    std::mutex cv_mutex_;         ///< 保护条件变量的互斥锁
};

}  // namespace Corona::Kernal::Utils