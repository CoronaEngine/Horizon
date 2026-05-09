/**
 * @file main.cpp
 * @brief Corona Framework 协程模块示例
 *
 * 展示 C++20 协程封装的基本用法
 */

#include <corona/kernel/coro/coro.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using namespace Corona::Kernel::Coro;

// ========================================
// 示例 1: 基本异步任务
// ========================================

Task<int> async_add(int a, int b) {
    // 关键点：suspend_for 会将协程提交给 Scheduler (默认是 TBB 线程池)
    // 当时间到达后，线程池中的某个空闲线程会恢复这个协程
    // 这就是为什么你会看到线程 ID 发生了变化
    co_await suspend_for(std::chrono::milliseconds{100});
    std::cout << "[Async] Adding " << a << " + " << b << " on thread " << std::this_thread::get_id() << std::endl;
    co_return a + b;
}

Task<int> compute_sum() {
    std::cout << "[Task] Starting computation on thread " << std::this_thread::get_id() << std::endl;

    int x = co_await async_add(1, 2);
    std::cout << "[Task] x = " << x << " (thread " << std::this_thread::get_id() << ")" << std::endl;

    int y = co_await async_add(3, 4);
    std::cout << "[Task] y = " << y << " (thread " << std::this_thread::get_id() << ")" << std::endl;

    int z = co_await async_add(x, y);
    std::cout << "[Task] z = " << z << " (thread " << std::this_thread::get_id() << ")" << std::endl;

    co_return z;
}

// ========================================
// 示例 2: 生成器 (始终在调用者线程运行)
// ========================================

Generator<int> fibonacci(int count) {
    std::cout << "[Gen] Started on thread " << std::this_thread::get_id() << std::endl;
    int a = 0, b = 1;
    for (int i = 0; i < count; ++i) {
        co_yield a;  // 挂起，将控制权还给调用者 (主线程)

        // 当调用者再次请求下一个值时，从这里恢复
        // 因为是调用者直接恢复的，所以还在原来的线程
        std::cout << "[Gen] Resumed on thread " << std::this_thread::get_id() << std::endl;

        int next = a + b;
        a = b;
        b = next;
    }
}

Generator<int> range(int start, int end) {
    for (int i = start; i < end; ++i) {
        co_yield i;
    }
}

Generator<int> filter_even(Generator<int> gen) {
    for (int n : gen) {
        if (n % 2 == 0) {
            co_yield n;
        }
    }
}

// ========================================
// 示例 3: 异常处理
// ========================================

Task<int> may_fail(bool should_fail) {
    co_await suspend_for_blocking(std::chrono::milliseconds{5});
    if (should_fail) {
        throw std::runtime_error("Something went wrong!");
    }
    co_return 42;
}

Task<std::string> handle_error() {
    try {
        int result = co_await may_fail(true);
        co_return "Success: " + std::to_string(result);
    } catch (const std::exception& e) {
        co_return std::string("Error: ") + e.what();
    }
}

// ========================================
// 示例 4: 条件等待
// ========================================

Task<void> wait_for_condition() {
    int counter = 0;

    std::cout << "[Wait] Waiting for counter to reach 5..." << std::endl;

    co_await wait_until([&counter]() {
        ++counter;
        return counter >= 5;
    },
                        std::chrono::milliseconds{10});

    std::cout << "[Wait] Counter reached: " << counter << std::endl;
}

// ========================================
// 示例 5: yield 让出执行
// ========================================

Task<void> cooperative_task(const std::string& name, int steps) {
    for (int i = 0; i < steps; ++i) {
        std::cout << "[" << name << "] Step " << i + 1 << "/" << steps << std::endl;
        co_await yield();  // 让出执行，允许其他协程运行
    }
    std::cout << "[" << name << "] Completed!" << std::endl;
}

// ========================================
// 示例 6: 嵌套协程 (对称转移)
// ========================================

Task<int> level_3() {
    std::cout << "  [Level 3] Computing..." << std::endl;
    co_await suspend_for_blocking(std::chrono::milliseconds{10});
    co_return 10;
}

Task<int> level_2() {
    std::cout << " [Level 2] Calling level 3..." << std::endl;
    int a = co_await level_3();
    int b = co_await level_3();
    co_return a + b;
}

Task<int> level_1() {
    std::cout << "[Level 1] Calling level 2..." << std::endl;
    int x = co_await level_2();
    int y = co_await level_2();
    co_return x* y;
}

// ========================================
// 示例 7: 生成器手动迭代 (next)
// ========================================

Generator<int> countdown(int start) {
    while (start > 0) {
        co_yield start--;
    }
}

Generator<std::string> string_generator() {
    co_yield "Hello";
    co_yield "Corona";
    co_yield "Framework";
    co_yield "Coroutine";
}

// ========================================
// 示例 8: 定时任务
// ========================================

Task<void> periodic_task(int count, std::chrono::milliseconds interval) {
    std::cout << "[Periodic] Starting periodic task..." << std::endl;
    for (int i = 0; i < count; ++i) {
        std::cout << "[Periodic] Tick " << i + 1 << "/" << count << std::endl;
        co_await suspend_for_blocking(interval);
    }
    std::cout << "[Periodic] Periodic task completed!" << std::endl;
}

// ========================================
// 示例 9: TbbExecutor 线程池与协程结合
// ========================================

Task<void> executor_demo_async() {
    std::cout << "[Executor] Started on thread: " << std::this_thread::get_id() << std::endl;

    // 切换到线程池执行
    co_await schedule_on_pool();
    std::cout << "[Executor] Running on pool thread: " << std::this_thread::get_id() << std::endl;

    // 模拟耗时操作
    co_await suspend_for(std::chrono::milliseconds{50});

    std::cout << "[Executor] Task completed on thread: " << std::this_thread::get_id() << std::endl;
}

void executor_demo() {
    // 使用 Runner 运行协程
    Runner::run(executor_demo_async());
}

// ========================================
// 示例 10: ready() 立即返回值
// ========================================

Task<int> use_ready_value() {
    // 将普通值包装为可 co_await 的形式
    int a = co_await ready(10);
    int b = co_await ready(20);

    // void 版本
    co_await ready();

    co_return a + b;
}

// ========================================
// 示例 11: 生成器组合 (map/filter)
// ========================================

Generator<int> map_double(Generator<int> gen) {
    for (int n : gen) {
        co_yield n * 2;
    }
}

Generator<int> take(Generator<int> gen, int count) {
    int i = 0;
    for (int n : gen) {
        if (i++ >= count) break;
        co_yield n;
    }
}

// ========================================
// 示例 12: 超时控制模式
// ========================================

Task<bool> with_timeout(std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    int attempts = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        attempts++;
        // 模拟检查某个条件
        bool condition_met = (attempts >= 3);
        if (condition_met) {
            std::cout << "[Timeout] Condition met after " << attempts << " attempts" << std::endl;
            co_return true;
        }
        co_await suspend_for_blocking(std::chrono::milliseconds{100});
    }

    std::cout << "[Timeout] Timed out after " << attempts << " attempts" << std::endl;
    co_return false;
}

// ========================================
// 示例 13: schedule_on_pool 切换到线程池
// ========================================

Task<int> async_work_on_pool() {
    std::cout << "[Pool] Before schedule, thread: " << std::this_thread::get_id() << std::endl;

    // 切换到线程池执行
    // 此时协程挂起，当前线程（如果是主线程）在 Task::get() 中等待
    // 协程将在线程池中的某个线程上恢复执行
    co_await schedule_on_pool();

    std::cout << "[Pool] Now on pool thread: " << std::this_thread::get_id() << std::endl;

    // 模拟一些计算
    int result = 0;
    for (int i = 0; i < 100; ++i) {
        result += i;
    }

    co_return result;
}

// ========================================
// 示例 14: ConditionVariable 事件驱动等待
// ========================================

// 全局变量用于演示
std::atomic<bool> g_data_ready{false};
std::shared_ptr<ConditionVariable> g_cv;

Task<void> producer_task() {
    std::cout << "[Producer] Preparing data..." << std::endl;
    co_await suspend_for_blocking(std::chrono::milliseconds{200});  // 模拟数据准备
    std::cout << "[Producer] Data ready! Notifying consumer..." << std::endl;
    g_data_ready = true;
    g_cv->notify_one();
    co_return;
}

Task<void> consumer_task() {
    std::cout << "[Consumer] Waiting for data (event-driven, no polling)..." << std::endl;
    co_await g_cv->wait([&]() { return g_data_ready.load(); });
    std::cout << "[Consumer] Data received! Processing..." << std::endl;
    co_return;
}

void condition_variable_demo() {
    g_cv = make_condition_variable();
    g_data_ready = false;

    // 在单独的线程中运行消费者（因为它会阻塞等待）
    std::thread consumer_thread([]() {
        auto task = consumer_task();
        task.get();
    });

    // 给消费者一点时间开始等待
    std::this_thread::sleep_for(std::chrono::milliseconds{50});

    // 运行生产者
    Runner::run(producer_task());

    consumer_thread.join();
    std::cout << "[Demo] Producer-Consumer demo completed!" << std::endl;
}

// ========================================
// 示例 15: ConditionVariable 带超时等待
// ========================================

Task<std::string> wait_with_timeout_demo() {
    auto cv = make_condition_variable();
    bool condition = false;

    // 尝试等待一个不会满足的条件，设置 100ms 超时
    std::cout << "[Timeout] Waiting with 100ms timeout..." << std::endl;
    bool timed_out = co_await cv->wait_for(
        [&]() { return condition; },
        std::chrono::milliseconds{100});

    if (timed_out) {
        co_return "Timed out (expected)";
    } else {
        co_return "Condition met";
    }
}

// ========================================
// 示例 16: suspend_for 调度器模式 vs 阻塞模式
// ========================================

Task<void> scheduler_mode_demo() {
    std::cout << "[Demo] Comparing suspend modes..." << std::endl;

    // 1. 阻塞模式 (suspend_for_blocking)
    // 直接阻塞当前线程，不涉及调度器。
    // 适用于：非协程环境、测试、或必须阻塞当前线程的场景。
    auto start = std::chrono::steady_clock::now();
    co_await suspend_for_blocking(std::chrono::milliseconds{50});
    auto end = std::chrono::steady_clock::now();
    std::cout << "[Blocking] Slept for "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << "ms on thread " << std::this_thread::get_id() << std::endl;

    // 2. 调度器模式 (suspend_for)
    // 挂起协程，将恢复任务提交给调度器（线程池）。
    // 当前线程可以去执行其他任务（如果是 Worker 线程）或等待（如果是 Main 线程）。
    // 适用于：真正的异步 I/O 或定时任务。
    start = std::chrono::steady_clock::now();
    co_await suspend_for(std::chrono::milliseconds{50});
    end = std::chrono::steady_clock::now();
    std::cout << "[Scheduler] Slept for "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << "ms, resumed on thread " << std::this_thread::get_id() << std::endl;
}

// ========================================
// Main
// ========================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Corona Framework Coroutine Examples" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    // 示例 1: 异步任务
    std::cout << "--- Example 1: Async Task ---" << std::endl;
    {
        std::cout << "[Main] Main thread ID: " << std::this_thread::get_id() << std::endl;
        std::cout << "[Main] Waiting for result..." << std::endl;
        int result = Runner::run(compute_sum());
        std::cout << "[Main] Final result: " << result << std::endl;
    }
    std::cout << std::endl;

    // 示例 2: 生成器
    std::cout << "--- Example 2: Generator ---" << std::endl;
    {
        std::cout << "Fibonacci sequence: ";
        for (int n : fibonacci(10)) {
            std::cout << n << " ";
        }
        std::cout << std::endl;

        std::cout << "Even numbers in range [0, 10): ";
        for (int n : filter_even(range(0, 10))) {
            std::cout << n << " ";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;

    // 示例 3: 异常处理
    std::cout << "--- Example 3: Exception Handling ---" << std::endl;
    {
        std::string result = Runner::run(handle_error());
        std::cout << "[Main] " << result << std::endl;
    }
    std::cout << std::endl;

    // 示例 4: 条件等待
    std::cout << "--- Example 4: Wait Until ---" << std::endl;
    {
        Runner::run(wait_for_condition());
    }
    std::cout << std::endl;

    // 示例 5: 使用 Runner
    std::cout << "--- Example 5: Using Runner ---" << std::endl;
    {
        int result = Runner::run(async_add(100, 200));
        std::cout << "[Runner] Result: " << result << std::endl;
    }
    std::cout << std::endl;

    // 示例 6: yield 让出执行
    std::cout << "--- Example 6: Yield (Cooperative Tasks) ---" << std::endl;
    {
        Runner::run(cooperative_task("TaskA", 3));
        Runner::run(cooperative_task("TaskB", 2));
    }
    std::cout << std::endl;

    // 示例 7: 嵌套协程
    std::cout << "--- Example 7: Nested Coroutines ---" << std::endl;
    {
        int result = Runner::run(level_1());
        std::cout << "[Nested] Final result: " << result << std::endl;
    }
    std::cout << std::endl;

    // 示例 8: 生成器手动迭代
    std::cout << "--- Example 8: Generator Manual Iteration ---" << std::endl;
    {
        std::cout << "Countdown: ";
        auto gen = countdown(5);
        while (auto value = gen.next()) {
            std::cout << *value << " ";
        }
        std::cout << std::endl;

        std::cout << "Strings: ";
        for (const auto& s : string_generator()) {
            std::cout << s << " ";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;

    // 示例 9: 定时任务
    std::cout << "--- Example 9: Periodic Task ---" << std::endl;
    {
        Runner::run(periodic_task(3, std::chrono::milliseconds{200}));
    }
    std::cout << std::endl;

    // 示例 10: TbbExecutor 线程池
    std::cout << "--- Example 10: TbbExecutor Thread Pool ---" << std::endl;
    {
        executor_demo();
    }
    std::cout << std::endl;

    // 示例 11: ready() 立即返回值
    std::cout << "--- Example 11: Ready Value ---" << std::endl;
    {
        int result = Runner::run(use_ready_value());
        std::cout << "[Ready] Result: " << result << std::endl;
    }
    std::cout << std::endl;

    // 示例 12: 生成器组合
    std::cout << "--- Example 12: Generator Composition ---" << std::endl;
    {
        // fibonacci 数的 2 倍，过滤偶数
        std::cout << "First 5 fibonacci doubled: ";
        for (int n : take(map_double(fibonacci(10)), 5)) {
            std::cout << n << " ";
        }
        std::cout << std::endl;

        // 范围过滤偶数后再取前 3 个
        std::cout << "First 3 even from range [0,20): ";
        for (int n : take(filter_even(range(0, 20)), 3)) {
            std::cout << n << " ";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;

    // 示例 13: 超时控制
    std::cout << "--- Example 13: Timeout Control ---" << std::endl;
    {
        bool success = Runner::run(with_timeout(std::chrono::milliseconds{500}));
        std::cout << "[Timeout] Result: " << (success ? "Success" : "Failed") << std::endl;
    }
    std::cout << std::endl;

    // 示例 14: ConditionVariable 事件驱动等待
    std::cout << "--- Example 14: ConditionVariable (Event-Driven Wait) ---" << std::endl;
    {
        condition_variable_demo();
    }
    std::cout << std::endl;

    // 示例 15: ConditionVariable 带超时等待
    std::cout << "--- Example 15: ConditionVariable with Timeout ---" << std::endl;
    {
        std::string result = Runner::run(wait_with_timeout_demo());
        std::cout << "[Result] " << result << std::endl;
    }
    std::cout << std::endl;

    // 示例 16: suspend_for 调度器模式 vs 阻塞模式
    std::cout << "--- Example 16: Scheduler vs Blocking Mode ---" << std::endl;
    {
        Runner::run(scheduler_mode_demo());
    }
    std::cout << std::endl;

    std::cout << "========================================" << std::endl;
    std::cout << "All examples completed!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
