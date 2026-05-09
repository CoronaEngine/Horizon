#include "corona/kernel/utils/work_stealing_queue.h"

#include <utility>

// 将任务推入队列尾部（所有者操作）
void Corona::Kernal::Utils::WorkStealingQueue::push(Task task) {
    std::lock_guard<std::mutex> lock(mutex_);  // 获取锁保护队列
    tasks_.push_back(std::move(task));         // 移动任务到队列尾部
}

// 从队列头部弹出任务（所有者操作，FIFO）
std::optional<Corona::Kernal::Utils::Task>
Corona::Kernal::Utils::WorkStealingQueue::try_pop() {
    std::lock_guard<std::mutex> lock(mutex_);  // 获取锁保护队列
    if (tasks_.empty()) {
        return std::nullopt;  // 队列为空，返回空
    }
    // 从头部取任务（FIFO 顺序）
    Task task = std::move(tasks_.front());
    tasks_.pop_front();
    return task;
}

// 从队列尾部窃取任务（窃取者操作，LIFO）
std::optional<Corona::Kernal::Utils::Task>
Corona::Kernal::Utils::WorkStealingQueue::try_steal() {
    std::lock_guard<std::mutex> lock(mutex_);  // 获取锁保护队列
    if (tasks_.empty()) {
        return std::nullopt;  // 队列为空，返回空
    }
    // 从尾部取任务（LIFO 顺序，与 try_pop 相反，减少竞争）
    Task task = std::move(tasks_.back());
    tasks_.pop_back();
    return task;
}
