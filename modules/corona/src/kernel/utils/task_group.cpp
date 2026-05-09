#include "corona/kernel/utils/task_group.h"

#include <atomic>

void Corona::Kernal::Utils::TaskGroup::wait() {
    // 阶段 1：主线程参与式等待，尝试执行任务避免空转
    // 当还有任务未完成时，主线程尝试从调度器中窃取任务执行
    while (task_count_.load(std::memory_order_acquire) > 0) {
        if (!scheduler_.try_execute_task()) {
            // 如果窃取失败（所有队列都为空），跳出循环进入休眠等待
            break;
        }
    }

    // 阶段 2：高效休眠等待（使用 C++20 atomic::wait）
    // 如果还有任务未完成，使用 atomic::wait 进入休眠
    // 最后一个完成的任务会通过 notify_all 唤醒
    // 使用 acquire 与任务完成时的 release 建立 synchronizes-with 关系
    std::size_t current_tasks = task_count_.load(std::memory_order_acquire);
    while (current_tasks > 0) {
        // atomic::wait 会阻塞直到 task_count_ 的值不等于 current_tasks
        task_count_.wait(current_tasks, std::memory_order_acquire);
        // 唤醒后重新加载当前值，继续检查
        current_tasks = task_count_.load(std::memory_order_acquire);
    }
}