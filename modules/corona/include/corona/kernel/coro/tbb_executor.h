#pragma once

#include <tbb/concurrent_queue.h>
#include <tbb/task_arena.h>
#include <tbb/task_group.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

#include "executor.h"

namespace Corona::Kernel::Coro {

/**
 * @brief 基于 TBB 的线程池执行器
 *
 * 使用 Intel TBB (Threading Building Blocks) 实现高效的任务调度。
 *
 * 特性：
 * - 工作窃取调度：自动负载均衡
 * - 可配置线程数：支持自定义线程池大小
 * - 延时任务：支持在指定时间后执行任务
 * - 优雅关闭：等待所有任务完成后再销毁
 *
 * 使用示例：
 * @code
 * TbbExecutor executor;  // 使用默认线程数
 *
 * executor.execute([]() {
 *     std::cout << "Task running on thread pool" << std::endl;
 * });
 *
 * executor.execute_after([]() {
 *     std::cout << "Delayed task" << std::endl;
 * }, std::chrono::milliseconds{1000});
 *
 * executor.wait();  // 等待所有任务完成
 * @endcode
 */
class TbbExecutor : public IExecutor {
   public:
    /**
     * @brief 使用默认线程数构造
     *
     * 线程数由 TBB 自动确定（通常等于硬件并发数）
     */
    TbbExecutor() : arena_(tbb::task_arena::automatic), running_(true) {
        start_timer_thread();
    }

    /**
     * @brief 指定线程数构造
     *
     * @param num_threads 线程池中的线程数量
     */
    explicit TbbExecutor(int num_threads) : arena_(num_threads), running_(true) {
        start_timer_thread();
    }

    /**
     * @brief 析构函数
     *
     * 自动调用 shutdown() 确保资源释放
     */
    ~TbbExecutor() noexcept override { shutdown(); }

    // 禁止拷贝和移动
    TbbExecutor(const TbbExecutor&) = delete;
    TbbExecutor& operator=(const TbbExecutor&) = delete;
    TbbExecutor(TbbExecutor&&) = delete;
    TbbExecutor& operator=(TbbExecutor&&) = delete;

    /**
     * @brief 提交任务到 TBB 线程池
     *
     * @param task 要执行的任务
     */
    void execute(std::function<void()> task) override {
        if (!running_ || !task) {
            return;
        }
        arena_.execute([this, task = std::move(task)]() { task_group_.run(task); });
    }

    /**
     * @brief 延迟执行任务
     *
     * @param task 要执行的任务
     * @param delay 延迟时间
     */
    void execute_after(std::function<void()> task,
                       std::chrono::milliseconds delay) override {
        if (!running_ || !task) {
            return;
        }
        auto deadline = std::chrono::steady_clock::now() + delay;
        delayed_tasks_.push(DelayedTask{std::move(task), deadline});
    }

    /**
     * @brief 检查是否在 TBB 线程池线程中
     *
     * @return 如果当前线程是 TBB 工作线程则返回 true
     */
    [[nodiscard]] bool is_in_executor_thread() const override {
        return tbb::this_task_arena::current_thread_index() >= 0;
    }

    /**
     * @brief 等待所有任务完成
     *
     * 阻塞直到所有已提交的任务执行完毕
     */
    void wait() {
        arena_.execute([this]() { task_group_.wait(); });
    }

    /**
     * @brief 关闭执行器
     *
     * 停止接受新任务，等待所有正在执行的任务完成
     */
    void shutdown() {
        if (running_.exchange(false)) {
            // 等待定时线程退出
            if (timer_thread_.joinable()) {
                timer_thread_.join();
            }
            // 等待所有任务完成
            wait();
        }
    }

    /**
     * @brief 检查执行器是否正在运行
     *
     * @return 如果执行器正在运行则返回 true
     */
    [[nodiscard]] bool is_running() const noexcept { return running_; }

    /**
     * @brief 获取最大并发数
     *
     * @return 线程池中的线程数量
     */
    [[nodiscard]] int max_concurrency() const { return arena_.max_concurrency(); }

   private:
    /**
     * @brief 延时任务结构
     */
    struct DelayedTask {
        std::function<void()> task;
        std::chrono::steady_clock::time_point deadline;
    };

    /**
     * @brief 启动定时器线程
     */
    void start_timer_thread() {
        timer_thread_ = std::thread([this]() { timer_loop(); });
    }

    /**
     * @brief 定时器循环
     *
     * 检查并执行到期的延时任务
     */
    void timer_loop() {
        std::vector<DelayedTask> pending;

        while (running_) {
            DelayedTask task;
            auto now = std::chrono::steady_clock::now();

            // 处理所有队列中的任务
            while (delayed_tasks_.try_pop(task)) {
                if (now >= task.deadline) {
                    // 任务到期，执行它
                    execute(std::move(task.task));
                } else {
                    // 任务未到期，暂存
                    pending.push_back(std::move(task));
                }
            }

            // 将未到期的任务重新入队
            for (auto& t : pending) {
                delayed_tasks_.push(std::move(t));
            }
            pending.clear();

            // 短暂休眠，避免忙等
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }

        // 关闭时执行所有剩余的延时任务
        DelayedTask task;
        while (delayed_tasks_.try_pop(task)) {
            if (task.task) {
                arena_.execute([this, t = std::move(task.task)]() { task_group_.run(t); });
            }
        }
    }

    tbb::task_arena arena_;
    tbb::task_group task_group_;
    tbb::concurrent_queue<DelayedTask> delayed_tasks_;
    std::thread timer_thread_;
    std::atomic<bool> running_;
};

}  // namespace Corona::Kernel::Coro
