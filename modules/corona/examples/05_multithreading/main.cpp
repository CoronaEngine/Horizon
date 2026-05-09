// Multithreading Example - 展示多线程事件处理
// 演示EventBus的线程安全性和System的并发执行

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "corona/kernel/core/kernel_context.h"
#include "corona/kernel/event/i_event_bus.h"
#include "corona/kernel/system/i_system_manager.h"
#include "corona/kernel/system/system_base.h"

using namespace Corona::Kernel;

// ========================================
// 自定义事件
// ========================================

struct WorkTask {
    int task_id;
    int worker_id;
    std::string description;
};

struct TaskCompleted {
    int task_id;
    int worker_id;
    double execution_time_ms;
};

struct StatisticsRequest {
    int request_id;
};

// ========================================
// 工作者系统
// ========================================

class WorkerSystem : public SystemBase {
   public:
    explicit WorkerSystem(int worker_id) : worker_id_(worker_id) {
        name_ = "WorkerSystem_" + std::to_string(worker_id);
    }

    std::string_view get_name() const override { return name_; }
    int get_priority() const override { return 50; }
    int get_target_fps() const override { return 30; }

    bool initialize(ISystemContext* ctx) override {
        ctx_ = ctx;

        // 订阅工作任务
        ctx->event_bus()->subscribe<WorkTask>([this](const WorkTask& task) {
            if (task.worker_id == worker_id_ || task.worker_id == -1) {
                process_task(task);
            }
        });

        return true;
    }

    void update() override {
        // 定期检查工作队列
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    void shutdown() override {
        CFW_LOG_INFO("{} completed {} tasks", name_, tasks_completed_.load());
    }

   private:
    void process_task(const WorkTask& task) {
        auto start = std::chrono::high_resolution_clock::now();

        // 模拟工作负载
        std::this_thread::sleep_for(std::chrono::milliseconds(50 + (task.task_id % 50)));

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double, std::milli>(end - start).count();

        tasks_completed_++;

        // 发布完成事件
        ctx_->event_bus()->publish(TaskCompleted{
            task.task_id,
            worker_id_,
            duration});
    }

    ISystemContext* ctx_ = nullptr;
    int worker_id_;
    std::string name_;
    std::atomic<int> tasks_completed_{0};
};

// ========================================
// 统计系统
// ========================================

class StatisticsSystem : public SystemBase {
   public:
    std::string_view get_name() const override { return "StatisticsSystem"; }
    int get_priority() const override { return 10; }
    int get_target_fps() const override { return 1; }

    bool initialize(ISystemContext* ctx) override {
        ctx_ = ctx;

        // 订阅任务完成事件
        ctx->event_bus()->subscribe<TaskCompleted>([this](const TaskCompleted& evt) {
            total_tasks_++;
            total_time_ += evt.execution_time_ms;

            if (evt.execution_time_ms > max_time_) {
                max_time_ = evt.execution_time_ms;
            }
            if (evt.execution_time_ms < min_time_) {
                min_time_ = evt.execution_time_ms;
            }
        });

        // 订阅统计请求
        ctx->event_bus()->subscribe<StatisticsRequest>([this](const StatisticsRequest&) {
            print_statistics();
        });

        return true;
    }

    void update() override {
        // 每秒打印一次统计
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    void shutdown() override {
        print_statistics();
    }

   private:
    void print_statistics() {
        if (total_tasks_ > 0) {
            double avg = total_time_ / total_tasks_;
            CFW_LOG_INFO("=== Statistics ===");
            CFW_LOG_INFO("  Total tasks: {}", total_tasks_.load());
            CFW_LOG_INFO("  Average time: {} ms", avg);
            CFW_LOG_INFO("  Min time: {} ms", min_time_);
            CFW_LOG_INFO("  Max time: {} ms", max_time_);
        }
    }

    ISystemContext* ctx_ = nullptr;
    std::atomic<int> total_tasks_{0};
    std::atomic<double> total_time_{0.0};
    double min_time_ = std::numeric_limits<double>::max();
    double max_time_ = 0.0;
};

// ========================================
// 主程序
// ========================================

int main() {
    std::cout << "=== Corona Framework - Multithreading Example ===" << std::endl;
    std::cout << std::endl;

    // 初始化内核
    auto& kernel = KernelContext::instance();
    if (!kernel.initialize()) {
        std::cerr << "Failed to initialize kernel!" << std::endl;
        return 1;
    }

    auto* system_manager = kernel.system_manager();
    auto* event_bus = kernel.event_bus();

    // ========================================
    // 示例 1: 线程安全的EventBus
    // ========================================
    std::cout << "[Example 1] Thread-Safe EventBus" << std::endl;
    std::cout << "  Multiple threads publishing events simultaneously..." << std::endl;

    std::atomic<int> event_count{0};

    // 订阅者
    event_bus->subscribe<WorkTask>([&event_count](const WorkTask& task) {
        event_count++;
    });

    // 创建多个生产者线程
    std::vector<std::thread> producers;
    for (int i = 0; i < 4; ++i) {
        producers.emplace_back([event_bus, i]() {
            for (int j = 0; j < 250; ++j) {
                event_bus->publish(WorkTask{
                    i * 250 + j,
                    i,
                    "Task from thread " + std::to_string(i)});
            }
        });
    }

    // 等待所有生产者完成
    for (auto& t : producers) {
        t.join();
    }

    // 等待事件处理完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "  [OK] Published 1000 events from 4 threads" << std::endl;
    std::cout << "  [OK] Received " << event_count << " events" << std::endl;
    std::cout << std::endl;

    // 清空订阅者
    event_count = 0;

    // ========================================
    // 示例 2: 并发系统执行
    // ========================================
    std::cout << "[Example 2] Concurrent System Execution" << std::endl;
    std::cout << "  Creating 4 worker systems and 1 statistics system..." << std::endl;

    // 注册工作者系统
    for (int i = 0; i < 4; ++i) {
        system_manager->register_system(std::make_shared<WorkerSystem>(i));
    }

    // 注册统计系统
    system_manager->register_system(std::make_shared<StatisticsSystem>());

    // 初始化并启动系统
    if (!system_manager->initialize_all()) {
        std::cerr << "Failed to initialize systems!" << std::endl;
        return 1;
    }

    system_manager->start_all();
    std::cout << "  [OK] All systems started" << std::endl;
    std::cout << std::endl;

    // ========================================
    // 示例 3: 分发工作任务
    // ========================================
    std::cout << "[Example 3] Distributing Tasks" << std::endl;
    std::cout << "  Publishing 100 tasks..." << std::endl;

    for (int i = 0; i < 100; ++i) {
        event_bus->publish(WorkTask{
            i,
            i % 4,  // 轮流分配给不同的worker
            "Task " + std::to_string(i)});

        // 稍微延迟以避免队列溢出
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "  [OK] Tasks published" << std::endl;
    std::cout << "  Processing tasks (this will take a while)..." << std::endl;
    std::cout << std::endl;

    // 等待任务处理
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // 请求统计
    event_bus->publish(StatisticsRequest{1});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << std::endl;

    // ========================================
    // 示例 4: 广播任务
    // ========================================
    std::cout << "[Example 4] Broadcasting Tasks" << std::endl;
    std::cout << "  Publishing 20 broadcast tasks (handled by all workers)..." << std::endl;

    for (int i = 0; i < 20; ++i) {
        event_bus->publish(WorkTask{
            100 + i,
            -1,  // -1 表示所有worker都处理
            "Broadcast task " + std::to_string(i)});

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "  [OK] Broadcast tasks published" << std::endl;
    std::cout << "  Processing..." << std::endl;
    std::cout << std::endl;

    // 等待处理完成
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // 最终统计
    event_bus->publish(StatisticsRequest{2});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << std::endl;

    // ========================================
    // 清理
    // ========================================
    std::cout << "[Cleanup] Stopping systems..." << std::endl;

    system_manager->stop_all();
    system_manager->shutdown_all();
    kernel.shutdown();

    std::cout << std::endl;
    std::cout << "=== Example completed successfully ===" << std::endl;
    return 0;
}
