#pragma once

#include <chrono>
#include <functional>
#include <thread>

namespace Corona::Kernel::Coro {

/**
 * @brief 执行器接口
 *
 * 定义任务调度和执行的抽象接口。
 * 执行器负责决定任务在何时、何地执行。
 */
class IExecutor {
   public:
    virtual ~IExecutor() = default;

    /**
     * @brief 提交任务立即执行
     *
     * 将任务提交到执行器的任务队列，尽快执行。
     *
     * @param task 要执行的任务
     */
    virtual void execute(std::function<void()> task) = 0;

    /**
     * @brief 延迟执行任务
     *
     * 在指定延时后执行任务。
     *
     * @param task 要执行的任务
     * @param delay 延迟时间
     */
    virtual void execute_after(std::function<void()> task,
                               std::chrono::milliseconds delay) = 0;

    /**
     * @brief 检查当前是否在执行器的线程上
     *
     * @return 如果在执行器管理的线程上则返回 true
     */
    [[nodiscard]] virtual bool is_in_executor_thread() const = 0;
};

/**
 * @brief 内联执行器
 *
 * 在当前线程直接执行任务，不进行任何调度。
 * 主要用于测试或简单场景。
 */
class InlineExecutor : public IExecutor {
   public:
    /**
     * @brief 立即在当前线程执行任务
     */
    void execute(std::function<void()> task) override {
        if (task) {
            task();
        }
    }

    /**
     * @brief 在当前线程延迟执行任务
     *
     * 注意：这会阻塞当前线程
     */
    void execute_after(std::function<void()> task,
                       std::chrono::milliseconds delay) override {
        std::this_thread::sleep_for(delay);
        if (task) {
            task();
        }
    }

    /**
     * @brief 总是返回 true
     */
    [[nodiscard]] bool is_in_executor_thread() const override { return true; }
};

/**
 * @brief 获取全局内联执行器实例
 *
 * @return 内联执行器的引用
 */
inline InlineExecutor& inline_executor() {
    static InlineExecutor instance;
    return instance;
}

}  // namespace Corona::Kernel::Coro
