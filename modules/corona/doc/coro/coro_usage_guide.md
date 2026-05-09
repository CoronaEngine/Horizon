# Corona Framework 协程模块用法指南

本指南详细介绍 Corona Framework 协程模块的所有用法，包括完整的代码示例。

## 目录

1. [快速开始](#1-快速开始)
2. [Task<T> 异步任务](#2-taskt-异步任务)
3. [Generator<T> 生成器](#3-generatort-生成器)
4. [Awaitable 等待器](#4-awaitable-等待器)
5. [执行器 (Executor)](#5-执行器-executor)
6. [调度器 (Scheduler)](#6-调度器-scheduler)
7. [Parallel Composition (when_all)](#7-parallel-composition-when_all)
8. [Synchronization Primitives](#8-synchronization-primitives)
9. [异常处理](#9-异常处理)
10. [高级用法](#10-高级用法)
11. [最佳实践](#11-最佳实践)

---

## 1. 快速开始

### 1.1 引入头文件

```cpp
#include <corona/kernel/coro/coro.h>

using namespace Corona::Kernel::Coro;
```

统一头文件 `coro.h` 包含所有协程模块 API。

### 1.2 最简示例

```cpp
#include <corona/kernel/coro/coro.h>
#include <iostream>

using namespace Corona::Kernel::Coro;

// 定义一个异步任务
Task<int> async_add(int a, int b) {
    co_return a + b;
}

int main() {
    // 运行任务并获取结果
    int result = Runner::run(async_add(1, 2));
    std::cout << "Result: " << result << std::endl;  // 输出: Result: 3
    return 0;
}
```

### 1.3 构建和运行

```powershell
cmake --preset ninja-msvc
cmake --build build --target 10_coroutine --config Debug
.\build\examples\Debug\10_coroutine.exe
```

---

## 2. Task<T> 异步任务

`Task<T>` 是最常用的协程返回类型，表示一个最终会产生 `T` 类型值的异步操作。

### 2.1 基本用法

```cpp
// 返回值任务
Task<int> compute_value() {
    co_return 42;
}

// void 任务
Task<void> do_something() {
    std::cout << "Doing something..." << std::endl;
    co_return;  // void 任务使用 co_return; 或省略
}

// 或者不写 co_return
Task<void> do_something_else() {
    std::cout << "Doing something else..." << std::endl;
}
```

### 2.2 组合多个任务

```cpp
Task<int> add(int a, int b) {
    co_return a + b;
}

Task<int> multiply(int a, int b) {
    co_return a * b;
}

// 组合多个任务
Task<int> compute() {
    int sum = co_await add(1, 2);       // sum = 3
    int product = co_await multiply(3, 4);  // product = 12
    co_return sum + product;             // 返回 15
}

// 运行
int result = Runner::run(compute());
```

### 2.3 惰性执行特性

`Task<T>` 是惰性的（lazy），创建后不会立即执行，需要 `co_await` 或调用 `get()` 才会启动：

```cpp
Task<int> lazy_task() {
    std::cout << "Task started!" << std::endl;  // 只有被 await 时才会打印
    co_return 42;
}

int main() {
    auto task = lazy_task();  // 任务已创建，但尚未执行
    std::cout << "Task created" << std::endl;
    
    int result = task.get();  // 现在开始执行
    std::cout << "Result: " << result << std::endl;
    return 0;
}
// 输出:
// Task created
// Task started!
// Result: 42
```

### 2.4 状态查询

```cpp
Task<int> some_task() {
    co_return 42;
}

int main() {
    auto task = some_task();
    
    // 检查任务是否有效
    if (task) {
        std::cout << "Task is valid" << std::endl;
    }
    
    // 检查任务是否完成
    if (!task.done()) {
        std::cout << "Task not done yet" << std::endl;
    }
    
    // 手动恢复执行
    task.resume();
    
    if (task.done()) {
        std::cout << "Task completed!" << std::endl;
    }
    
    return 0;
}
```

### 2.5 同步等待 (get)

`get()` 方法会阻塞当前线程直到任务完成：

```cpp
Task<std::string> fetch_data() {
    co_await suspend_for(std::chrono::milliseconds{100});
    co_return "Hello, World!";
}

int main() {
    auto task = fetch_data();
    
    // 阻塞等待结果
    std::string data = task.get();
    std::cout << data << std::endl;
    
    return 0;
}
```

> **注意**: `get()` 会阻塞当前线程，仅适用于测试或主函数。在协程内部应使用 `co_await`。

---

## 3. Generator<T> 生成器

`Generator<T>` 用于惰性生成值序列，支持 `co_yield` 语法。

### 3.1 基本用法

```cpp
Generator<int> simple_range(int n) {
    for (int i = 0; i < n; ++i) {
        co_yield i;
    }
}

int main() {
    // 使用 range-based for 循环
    for (int n : simple_range(5)) {
        std::cout << n << " ";  // 输出: 0 1 2 3 4
    }
    std::cout << std::endl;
    return 0;
}
```

### 3.2 Fibonacci 序列

```cpp
Generator<int> fibonacci(int count) {
    int a = 0, b = 1;
    for (int i = 0; i < count; ++i) {
        co_yield a;
        int next = a + b;
        a = b;
        b = next;
    }
}

int main() {
    std::cout << "Fibonacci: ";
    for (int n : fibonacci(10)) {
        std::cout << n << " ";  // 输出: 0 1 1 2 3 5 8 13 21 34
    }
    std::cout << std::endl;
    return 0;
}
```

### 3.3 手动迭代 (next)

```cpp
Generator<int> countdown(int start) {
    while (start > 0) {
        co_yield start--;
    }
}

int main() {
    auto gen = countdown(5);
    
    while (auto value = gen.next()) {
        std::cout << *value << " ";  // 输出: 5 4 3 2 1
    }
    std::cout << std::endl;
    
    return 0;
}
```

### 3.4 生成器组合

```cpp
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

Generator<int> map_square(Generator<int> gen) {
    for (int n : gen) {
        co_yield n * n;
    }
}

int main() {
    // 组合: 0-9 中的偶数的平方
    std::cout << "Even squares: ";
    for (int n : map_square(filter_even(range(0, 10)))) {
        std::cout << n << " ";  // 输出: 0 4 16 36 64
    }
    std::cout << std::endl;
    return 0;
}
```

### 3.5 状态查询

```cpp
Generator<int> limited_gen() {
    co_yield 1;
    co_yield 2;
}

int main() {
    auto gen = limited_gen();
    
    // 检查生成器是否有效
    if (gen) {
        std::cout << "Generator is valid" << std::endl;
    }
    
    gen.next();  // 生成 1
    gen.next();  // 生成 2
    
    // 检查是否已完成
    if (gen.done()) {
        std::cout << "Generator exhausted" << std::endl;
    }
    
    return 0;
}
```

---

## 4. Awaitable 等待器

等待器是可以被 `co_await` 的对象，协程模块提供多种内置等待器。

### 4.1 suspend_for - 延时等待

`suspend_for` supports two modes:
- **Scheduler mode** (default): Uses timer scheduler, non-blocking
- **Blocking mode**: Uses sleep, blocks current thread, compatible with `Task::get()`

```cpp
Task<void> delay_example() {
    std::cout << "Starting..." << std::endl;
    
    // Scheduler mode (default) - non-blocking, requires async executor
    co_await suspend_for(std::chrono::milliseconds{100});
    
    std::cout << "After 100ms" << std::endl;
    
    // Blocking mode - compatible with Task::get()
    co_await suspend_for_blocking(std::chrono::milliseconds{50});
    
    std::cout << "After 50ms" << std::endl;
    
    // Wait 1.5 seconds (blocking mode)
    co_await suspend_for_seconds(1.5);
    
    std::cout << "After 1.5 seconds" << std::endl;
}
```

> **Note**: When using `Task::get()` to wait synchronously, prefer `suspend_for_blocking` as scheduler mode requires an async executor.

### 4.2 yield - 让出执行

```cpp
Task<void> cooperative_task() {
    for (int i = 0; i < 3; ++i) {
        std::cout << "Step " << i << std::endl;
        
        // 让出执行，允许其他协程运行
        co_await yield();
    }
}
```

### 4.3 ConditionVariable - Event-Driven Wait (Recommended)

`ConditionVariable` provides true event-driven waiting, avoiding CPU waste from polling:

```cpp
// Basic usage: Producer-Consumer pattern
std::atomic<bool> data_ready{false};
auto cv = make_condition_variable();

Task<void> consumer_task() {
    std::cout << "Waiting for data..." << std::endl;
    
    // Event-driven wait, no CPU consumption
    co_await cv->wait([&]() { return data_ready.load(); });
    
    std::cout << "Data is ready!" << std::endl;
}

Task<void> producer_task() {
    co_await suspend_for_blocking(std::chrono::milliseconds{200});
    data_ready = true;
    cv->notify_one();  // Notify waiter
}
```

#### Wait with Timeout

```cpp
Task<std::string> wait_with_timeout_demo() {
    auto cv = make_condition_variable();
    bool condition = false;

    // Wait for condition, max 100ms
    bool timed_out = co_await cv->wait_for(
        [&]() { return condition; },
        std::chrono::milliseconds{100});

    if (timed_out) {
        co_return "Timed out";
    } else {
        co_return "Condition met";
    }
}
```

#### Notify Multiple Waiters

```cpp
auto cv = make_condition_variable();

// Notify one waiter
cv->notify_one();

// Notify all waiters
cv->notify_all();
```

### 4.4 wait_until - Polling Condition Wait (Deprecated)

> **Note**: `wait_until` uses polling which consumes CPU resources. Use `ConditionVariable` instead.

```cpp
Task<void> wait_for_ready() {
    std::atomic<bool> ready{false};
    
    // Set ready in another thread
    std::thread worker([&ready]() {
        std::this_thread::sleep_for(std::chrono::milliseconds{500});
        ready = true;
    });
    
    std::cout << "Waiting for ready flag..." << std::endl;
    
    // Polling wait (not recommended, use ConditionVariable instead)
    co_await wait_until([&ready]() { return ready.load(); });
    
    std::cout << "Ready!" << std::endl;
    
    worker.join();
}

// Version with polling interval
Task<void> wait_with_interval() {
    int counter = 0;
    
    // Check every 50ms
    co_await wait_until(
        [&counter]() { return ++counter >= 10; },
        std::chrono::milliseconds{50}
    );
    
    std::cout << "Counter reached: " << counter << std::endl;
}
```

### 4.5 ready - 立即返回值

```cpp
Task<int> immediate_value() {
    // 包装普通值为可 co_await 的形式
    int value = co_await ready(42);
    co_return value;
}

Task<void> immediate_void() {
    // void 版本
    co_await ready();
    std::cout << "Immediate continuation" << std::endl;
}
```

### 4.6 switch_to - 切换执行器

```cpp
Task<void> switch_executor_example() {
    TbbExecutor executor(4);
    
    std::cout << "Before switch, thread: " 
              << std::this_thread::get_id() << std::endl;
    
    // 切换到指定执行器
    co_await switch_to(executor);
    
    std::cout << "After switch, thread: " 
              << std::this_thread::get_id() << std::endl;
    
    executor.shutdown();
}

// 延迟切换
Task<void> delayed_switch_example() {
    TbbExecutor executor(4);
    
    // 500ms 后在执行器上恢复
    co_await switch_to_after(executor, std::chrono::milliseconds{500});
    
    std::cout << "Resumed on executor after delay" << std::endl;
    
    executor.shutdown();
}
```

---

## 5. 执行器 (Executor)

执行器负责决定任务在何时、何地执行。

### 5.1 IExecutor 接口

```cpp
class IExecutor {
public:
    virtual ~IExecutor() = default;
    
    // 提交任务立即执行
    virtual void execute(std::function<void()> task) = 0;
    
    // 延迟执行任务
    virtual void execute_after(std::function<void()> task,
                               std::chrono::milliseconds delay) = 0;
    
    // 检查是否在执行器线程上
    virtual bool is_in_executor_thread() const = 0;
};
```

### 5.2 TbbExecutor - TBB 线程池执行器

```cpp
#include <corona/kernel/coro/coro.h>

// 使用默认线程数 (硬件并发数)
TbbExecutor executor1;

// 指定线程数
TbbExecutor executor2(4);

// 提交任务
executor2.execute([]() {
    std::cout << "Running on thread pool" << std::endl;
});

// 延迟执行
executor2.execute_after([]() {
    std::cout << "Delayed task" << std::endl;
}, std::chrono::milliseconds{1000});

// 等待所有任务完成
executor2.wait();

// 获取最大并发数
std::cout << "Max concurrency: " << executor2.max_concurrency() << std::endl;

// 关闭执行器 (析构时自动调用)
executor2.shutdown();
```

### 5.3 InlineExecutor - 内联执行器

```cpp
// 获取全局内联执行器
auto& executor = inline_executor();

// 任务在当前线程直接执行
executor.execute([]() {
    std::cout << "Inline execution" << std::endl;
});

// 延迟执行 (阻塞当前线程)
executor.execute_after([]() {
    std::cout << "After delay" << std::endl;
}, std::chrono::milliseconds{100});

// 总是返回 true
bool in_thread = executor.is_in_executor_thread();
```

### 5.4 在协程中使用执行器

```cpp
Task<void> executor_usage() {
    TbbExecutor pool(4);
    
    // 在主线程开始
    std::cout << "Main thread: " << std::this_thread::get_id() << std::endl;
    
    // 切换到线程池
    co_await switch_to(pool);
    
    std::cout << "Pool thread: " << std::this_thread::get_id() << std::endl;
    
    // 切换回内联执行器
    co_await switch_to(inline_executor());
    
    pool.shutdown();
}
```

---

## 6. 调度器 (Scheduler)

调度器管理协程的全局调度和执行。

### 6.1 Scheduler 单例

```cpp
// 获取调度器单例
auto& scheduler = Scheduler::instance();

// 设置默认执行器
auto executor = std::make_shared<TbbExecutor>(8);
scheduler.set_default_executor(executor);

// 获取默认执行器
IExecutor& default_exec = scheduler.default_executor();

// 调度协程句柄
std::coroutine_handle<> handle = /* ... */;
scheduler.schedule(handle);

// 延迟调度
scheduler.schedule_after(handle, std::chrono::milliseconds{100});

// 重置调度器 (清除默认执行器)
scheduler.reset();
```

### 6.2 schedule_on_pool - 切换到线程池

```cpp
Task<void> pool_task() {
    std::cout << "Before: " << std::this_thread::get_id() << std::endl;
    
    // 切换到调度器管理的默认线程池
    co_await schedule_on_pool();
    
    std::cout << "On pool: " << std::this_thread::get_id() << std::endl;
}

Task<void> delayed_pool_task() {
    // 500ms 后在线程池上恢复
    co_await schedule_on_pool_after(std::chrono::milliseconds{500});
    
    std::cout << "Resumed on pool after delay" << std::endl;
}
```

### 6.3 Runner - 协程运行器

```cpp
// 运行返回值任务
Task<int> compute() {
    co_return 42;
}

int result = Runner::run(compute());
std::cout << "Result: " << result << std::endl;

// 运行 void 任务
Task<void> work() {
    std::cout << "Working..." << std::endl;
    co_return;
}

Runner::run(work());
```

---

## 7. Parallel Composition (when_all)

The `when_all` function allows parallel waiting for multiple tasks to complete. This is the recommended way to implement concurrency.

### 7.1 Basic Usage

```cpp
Task<int> fetch_data_a() {
    co_await suspend_for(std::chrono::milliseconds{100});
    co_return 10;
}

Task<int> fetch_data_b() {
    co_await suspend_for(std::chrono::milliseconds{150});
    co_return 20;
}

Task<void> parallel_example() {
    // Wait for both tasks in parallel
    auto [a, b] = co_await when_all(fetch_data_a(), fetch_data_b());
    std::cout << "Results: " << a << ", " << b << std::endl;  // Output: Results: 10, 20
}
```

### 7.2 Tasks with Different Types

`when_all` supports tasks with different return types:

```cpp
Task<int> get_number() {
    co_return 42;
}

Task<std::string> get_string() {
    co_return "hello";
}

Task<void> do_work() {
    std::cout << "Working..." << std::endl;
    co_return;
}

Task<void> mixed_parallel() {
    // void tasks correspond to std::monostate
    auto [num, str, _] = co_await when_all(
        get_number(),
        get_string(),
        do_work()
    );
    std::cout << "Number: " << num << ", String: " << str << std::endl;
}
```

### 7.3 Vector of Same-Type Tasks

For multiple tasks of the same type, you can use `std::vector`:

```cpp
Task<int> fetch_item(int id) {
    co_await suspend_for(std::chrono::milliseconds{50});
    co_return id * 10;
}

Task<void> batch_fetch() {
    std::vector<Task<int>> tasks;
    for (int i = 0; i < 5; ++i) {
        tasks.push_back(fetch_item(i));
    }
    
    // Execute all tasks in parallel
    auto results = co_await when_all(std::move(tasks));
    
    for (int val : results) {
        std::cout << val << " ";  // Output: 0 10 20 30 40
    }
}
```

### 7.4 Exception Handling

If any task throws an exception, `when_all` catches the first exception and rethrows it:

```cpp
Task<int> may_fail(bool fail) {
    if (fail) {
        throw std::runtime_error("Task failed");
    }
    co_return 42;
}

Task<void> handle_errors() {
    try {
        auto [a, b] = co_await when_all(
            may_fail(false),
            may_fail(true)  // This will throw
        );
    } catch (const std::exception& e) {
        std::cout << "Caught exception: " << e.what() << std::endl;
    }
}
```

---

## 8. Synchronization Primitives

The coroutine module provides coroutine-friendly synchronization primitives that avoid blocking threads.

### 8.1 AsyncMutex - Async Mutex

`AsyncMutex` is a coroutine-friendly mutex that suspends coroutines instead of blocking threads when the lock is held:

```cpp
AsyncMutex mutex;
int shared_counter = 0;

Task<void> increment_counter() {
    // Acquire the lock
    co_await mutex.lock();
    
    // Critical section
    ++shared_counter;
    
    // Release the lock
    mutex.unlock();
}
```

#### Using ScopedLock for Automatic Release

```cpp
Task<void> safe_increment() {
    // Automatically acquire and release lock
    auto guard = co_await mutex.lock_scoped();
    
    // Critical section - guard releases lock on destruction
    ++shared_counter;
    co_await some_async_operation();
    ++shared_counter;
}
```

#### Concurrency-Safe Example

```cpp
Task<void> concurrent_updates() {
    AsyncMutex mutex;
    int counter = 0;
    
    // Launch multiple concurrent tasks
    std::vector<Task<void>> tasks;
    for (int i = 0; i < 10; ++i) {
        tasks.push_back([&]() -> Task<void> {
            for (int j = 0; j < 100; ++j) {
                auto guard = co_await mutex.lock_scoped();
                ++counter;
            }
        }());
    }
    
    co_await when_all(std::move(tasks));
    std::cout << "Final count: " << counter << std::endl;  // Output: 1000
}
```

### 8.2 AsyncScope - Async Scope

`AsyncScope` manages the lifecycle of a group of concurrent tasks, supporting "fire-and-forget" pattern:

```cpp
Task<void> worker_task(int id) {
    std::cout << "Worker " << id << " started" << std::endl;
    co_await suspend_for(std::chrono::milliseconds{100});
    std::cout << "Worker " << id << " finished" << std::endl;
}

Task<void> scope_example() {
    AsyncScope scope;
    
    // Spawn multiple tasks (fire-and-forget)
    scope.spawn(worker_task(1));
    scope.spawn(worker_task(2));
    scope.spawn(worker_task(3));
    
    std::cout << "All tasks spawned" << std::endl;
    
    // Wait for all tasks to complete
    co_await scope.join();
    
    std::cout << "All tasks completed" << std::endl;
}
```

#### Structured Concurrency

`AsyncScope` ensures all tasks are finished before the scope is destroyed:

```cpp
Task<void> structured_concurrency() {
    {
        AsyncScope scope;
        
        // Spawn background tasks
        scope.spawn(background_work());
        scope.spawn(background_work());
        
        // Perform other operations
        co_await main_work();
        
        // Wait for all tasks before scope destruction
        co_await scope.join();
    }  // All tasks guaranteed to be complete
}
```

---

## 9. 异常处理

协程模块支持完整的异常传播机制。

### 9.1 Task 中的异常

```cpp
Task<int> may_fail(bool should_fail) {
    co_await suspend_for(std::chrono::milliseconds{10});
    
    if (should_fail) {
        throw std::runtime_error("Task failed!");
    }
    
    co_return 42;
}

Task<void> caller() {
    try {
        int result = co_await may_fail(true);
        std::cout << "Result: " << result << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Caught: " << e.what() << std::endl;
    }
}

// 使用 get() 时的异常
int main() {
    try {
        auto task = may_fail(true);
        int result = task.get();  // 异常在这里抛出
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    return 0;
}
```

### 9.2 Generator 中的异常

```cpp
Generator<int> gen_with_error() {
    co_yield 1;
    co_yield 2;
    throw std::runtime_error("Generator error!");
    co_yield 3;  // 永远不会执行
}

int main() {
    try {
        for (int n : gen_with_error()) {
            std::cout << n << " ";  // 输出: 1 2
        }
    } catch (const std::exception& e) {
        std::cout << "\nError: " << e.what() << std::endl;
    }
    return 0;
}
```

### 9.3 错误处理模式

```cpp
// 使用 std::expected 风格 (C++23) 或类似模式
Task<std::optional<int>> safe_operation() {
    try {
        int result = co_await may_fail(false);
        co_return result;
    } catch (...) {
        co_return std::nullopt;
    }
}

// 使用字符串结果
Task<std::string> operation_with_message() {
    try {
        int result = co_await may_fail(true);
        co_return "Success: " + std::to_string(result);
    } catch (const std::exception& e) {
        co_return std::string("Error: ") + e.what();
    }
}
```

---

## 10. 高级用法

### 10.1 嵌套协程

```cpp
Task<int> inner_task() {
    co_await suspend_for(std::chrono::milliseconds{10});
    co_return 1;
}

Task<int> middle_task() {
    int a = co_await inner_task();
    int b = co_await inner_task();
    co_return a + b;
}

Task<int> outer_task() {
    int x = co_await middle_task();
    int y = co_await middle_task();
    co_return x * y;
}

// 对称转移确保不会栈溢出
int result = Runner::run(outer_task());  // result = 4
```

### 10.2 并行任务

Recommend using `when_all` for parallel tasks (see Section 7). Here is a legacy example using threads:

```cpp
Task<int> fetch_from_source_a() {
    co_await suspend_for(std::chrono::milliseconds{100});
    co_return 10;
}

Task<int> fetch_from_source_b() {
    co_await suspend_for(std::chrono::milliseconds{150});
    co_return 20;
}

// Recommended: Use when_all
Task<int> parallel_fetch_recommended() {
    auto [a, b] = co_await when_all(
        fetch_from_source_a(),
        fetch_from_source_b()
    );
    co_return a + b;
}

// Legacy: Using threads (not recommended)
Task<int> parallel_fetch_legacy() {
    std::atomic<int> result_a{0};
    std::atomic<int> result_b{0};
    
    std::thread t1([&]() {
        result_a = Runner::run(fetch_from_source_a());
    });
    
    std::thread t2([&]() {
        result_b = Runner::run(fetch_from_source_b());
    });
    
    t1.join();
    t2.join();
    
    co_return result_a.load() + result_b.load();
}
```

### 10.3 生成器与任务结合

```cpp
Generator<int> data_source() {
    for (int i = 1; i <= 5; ++i) {
        co_yield i * 10;
    }
}

Task<int> process_all() {
    int sum = 0;
    for (int value : data_source()) {
        sum += value;
        co_await yield();  // 让出执行
    }
    co_return sum;  // 返回 150
}
```

### 10.4 定时任务

```cpp
Task<void> periodic_task(int count, std::chrono::milliseconds interval) {
    for (int i = 0; i < count; ++i) {
        std::cout << "Tick " << i << std::endl;
        co_await suspend_for(interval);
    }
    std::cout << "Done!" << std::endl;
}

int main() {
    // 每 500ms 执行一次，共 5 次
    Runner::run(periodic_task(5, std::chrono::milliseconds{500}));
    return 0;
}
```

### 10.5 超时控制

```cpp
Task<bool> with_timeout() {
    auto deadline = std::chrono::steady_clock::now() + 
                    std::chrono::seconds{5};
    
    while (std::chrono::steady_clock::now() < deadline) {
        // 检查某个条件
        bool condition_met = check_condition();
        if (condition_met) {
            co_return true;
        }
        
        co_await suspend_for(std::chrono::milliseconds{100});
    }
    
    co_return false;  // 超时
}
```

---

## 11. 最佳实践

### 11.1 生命周期管理

```cpp
// ✅ 正确: Task 在作用域内保持有效
void good_example() {
    auto task = compute_sum();
    int result = task.get();
}

// ❌ 错误: 悬空引用
Task<int>& bad_example() {
    auto task = compute_sum();
    return task;  // task 将被销毁!
}

// ✅ 正确: 使用移动语义
Task<int> transfer_ownership() {
    auto task = compute_sum();
    return task;  // 移动语义
}
```

### 11.2 避免阻塞

```cpp
// ❌ 避免在协程中阻塞
Task<void> blocking_bad() {
    std::this_thread::sleep_for(std::chrono::seconds{1});  // 不好
    co_return;
}

// ✅ 使用协程等待
Task<void> non_blocking_good() {
    co_await suspend_for(std::chrono::seconds{1});  // 好
    co_return;
}
```

### 11.3 资源清理

```cpp
Task<void> resource_example() {
    // 使用 RAII 确保资源清理
    std::unique_ptr<Resource> resource = acquire_resource();
    
    try {
        co_await process(resource.get());
    } catch (...) {
        // resource 会被自动清理
        throw;
    }
    
    // resource 在这里自动释放
}
```

### 11.4 异常安全

```cpp
Task<void> exception_safe() {
    // 总是捕获可能的异常
    try {
        co_await risky_operation();
    } catch (const std::exception& e) {
        log_error(e.what());
        // 决定是重新抛出还是处理
        throw;
    }
}
```

### 11.5 性能考虑

```cpp
// ✅ 使用移动语义避免拷贝
Task<std::vector<int>> return_large_data() {
    std::vector<int> data(10000);
    // 填充数据...
    co_return std::move(data);  // 移动而非拷贝
}

// ✅ 生成器避免一次性生成所有数据
Generator<int> lazy_data() {
    for (int i = 0; i < 1000000; ++i) {
        co_yield compute(i);  // 按需计算
    }
}
```

---

## 附录：API 速查表

### Task<T>

| 方法 | 说明 |
|------|------|
| `Task()` | 默认构造 |
| `~Task()` | 析构，销毁协程帧 |
| `Task(Task&&)` | 移动构造 |
| `operator bool()` | 检查是否有效 |
| `done()` | 检查是否完成 |
| `resume()` | 恢复执行 |
| `get()` | 阻塞等待结果 |
| `handle()` | 获取底层句柄 |

### Generator<T>

| 方法 | 说明 |
|------|------|
| `Generator()` | 默认构造 |
| `begin()` | 获取起始迭代器 |
| `end()` | 获取结束迭代器 |
| `next()` | 获取下一个值 (optional) |
| `done()` | 检查是否完成 |
| `operator bool()` | 检查是否有效 |

### Awaitable Functions

| Function | Description |
|----------|-------------|
| `suspend_for(duration)` | Delay wait (scheduler mode, non-blocking) |
| `suspend_for_blocking(duration)` | Delay wait (blocking mode, compatible with Task::get()) |
| `suspend_for_seconds(secs)` | Delay in seconds (blocking mode) |
| `yield()` | Yield execution |
| `make_condition_variable()` | Create condition variable |
| `cv->wait(pred)` | Event-driven wait for condition |
| `cv->wait_for(pred, timeout)` | Event-driven wait with timeout |
| `cv->notify_one()` | Notify one waiter |
| `cv->notify_all()` | Notify all waiters |
| `wait_until(pred)` | Polling wait for condition (deprecated) |
| `wait_until(pred, interval)` | Polling wait with interval (deprecated) |
| `ready(value)` | Immediate return value |
| `ready()` | Immediate return void |
| `switch_to(executor)` | Switch to executor |
| `switch_to_after(executor, delay)` | Delayed switch to executor |
| `schedule_on_pool()` | Switch to default thread pool |
| `schedule_on_pool_after(delay)` | Delayed switch to thread pool |

### TbbExecutor

| 方法 | 说明 |
|------|------|
| `TbbExecutor()` | 默认线程数构造 |
| `TbbExecutor(n)` | 指定 n 个线程 |
| `execute(task)` | 提交任务 |
| `execute_after(task, delay)` | 延迟执行 |
| `wait()` | 等待所有任务 |
| `shutdown()` | 关闭执行器 |
| `is_running()` | 检查是否运行中 |
| `max_concurrency()` | 获取最大并发数 |

### Scheduler

| 方法 | 说明 |
|------|------|
| `instance()` | 获取单例 |
| `set_default_executor(exec)` | 设置默认执行器 |
| `default_executor()` | 获取默认执行器 |
| `schedule(handle)` | 调度协程 |
| `schedule_after(handle, delay)` | 延迟调度 |
| `reset()` | 重置调度器 |

### Runner

| Method | Description |
|--------|-------------|
| `run(Task<T>)` | Block and run task, return result |
| `run(Task<void>)` | Block and run void task |
| `spawn(Task<T>&&)` | Spawn task to thread pool (don't wait) |

### when_all

| Function | Description |
|----------|-------------|
| `when_all(Tasks...)` | Wait for multiple tasks of different types in parallel |
| `when_all(vector<Task<T>>)` | Wait for vector of same-type tasks in parallel |

### AsyncMutex

| Method | Description |
|--------|-------------|
| `lock()` | Acquire lock (returns Awaitable) |
| `lock_scoped()` | Acquire lock and return ScopedLock |
| `unlock()` | Release lock |

### AsyncScope

| Method | Description |
|--------|-------------|
| `spawn(Task<T>&&)` | Spawn task (fire-and-forget) |
| `join()` | Wait for all tasks to complete (returns Awaitable) |

---

## 参考资料

- [C++20 协程规范](https://en.cppreference.com/w/cpp/language/coroutines)
- [Corona 协程设计文档](coroutine_design.md)
- [示例代码](../../examples/10_coroutine/main.cpp)
