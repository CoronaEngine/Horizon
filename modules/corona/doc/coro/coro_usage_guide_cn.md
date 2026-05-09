# Corona Framework 协程模块用法指南 (中文版)

本指南详细介绍 Corona Framework 协程模块的所有用法，包括完整的代码示例。

## 目录

1. [快速开始](#1-快速开始)
2. [Task<T> 异步任务](#2-taskt-异步任务)
3. [Generator<T> 生成器](#3-generatort-生成器)
4. [Awaitable 等待器](#4-awaitable-等待器)
5. [执行器 (Executor)](#5-执行器-executor)
6. [调度器 (Scheduler)](#6-调度器-scheduler)
7. [并行组合 (when_all)](#7-并行组合-when_all)
8. [同步原语](#8-同步原语)
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
    std::cout << "结果: " << result << std::endl;  // 输出: 结果: 3
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
// 带返回值的任务
Task<int> compute_value() {
    co_return 42;
}

// void 任务
Task<void> do_something() {
    std::cout << "正在执行..." << std::endl;
    co_return;  // void 任务使用 co_return; 或省略
}

// 或者不写 co_return
Task<void> do_something_else() {
    std::cout << "正在执行其他操作..." << std::endl;
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
    int sum = co_await add(1, 2);           // sum = 3
    int product = co_await multiply(3, 4);  // product = 12
    co_return sum + product;                // 返回 15
}

// 运行
int result = Runner::run(compute());
```

### 2.3 惰性执行特性

`Task<T>` 是惰性的（lazy），创建后不会立即执行，需要 `co_await` 或调用 `get()` 才会启动：

```cpp
Task<int> lazy_task() {
    std::cout << "任务开始执行!" << std::endl;  // 只有被 await 时才会打印
    co_return 42;
}

int main() {
    auto task = lazy_task();  // 任务已创建，但尚未执行
    std::cout << "任务已创建" << std::endl;
    
    int result = task.get();  // 现在开始执行
    std::cout << "结果: " << result << std::endl;
    return 0;
}
// 输出:
// 任务已创建
// 任务开始执行!
// 结果: 42
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
        std::cout << "任务有效" << std::endl;
    }
    
    // 检查任务是否完成
    if (!task.done()) {
        std::cout << "任务尚未完成" << std::endl;
    }
    
    // 手动恢复执行
    task.resume();
    
    if (task.done()) {
        std::cout << "任务已完成!" << std::endl;
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
    std::cout << "斐波那契数列: ";
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
    std::cout << "偶数的平方: ";
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
        std::cout << "生成器有效" << std::endl;
    }
    
    gen.next();  // 生成 1
    gen.next();  // 生成 2
    
    // 检查是否已完成
    if (gen.done()) {
        std::cout << "生成器已耗尽" << std::endl;
    }
    
    return 0;
}
```

---

## 4. Awaitable 等待器

等待器是可以被 `co_await` 的对象，协程模块提供多种内置等待器。

### 4.1 suspend_for - 延时等待

`suspend_for` 支持两种模式：
- **调度器模式**（默认）：使用定时器调度器，不阻塞线程
- **阻塞模式**：使用 sleep，会阻塞当前线程，与 `Task::get()` 兼容

```cpp
Task<void> delay_example() {
    std::cout << "开始..." << std::endl;
    
    // 调度器模式（默认）- 不阻塞线程，需配合异步执行器
    co_await suspend_for(std::chrono::milliseconds{100});
    
    std::cout << "100毫秒后" << std::endl;
    
    // 阻塞模式 - 与 Task::get() 兼容
    co_await suspend_for_blocking(std::chrono::milliseconds{50});
    
    std::cout << "50毫秒后" << std::endl;
    
    // 等待 1.5 秒 (使用秒数，阻塞模式)
    co_await suspend_for_seconds(1.5);
    
    std::cout << "1.5秒后" << std::endl;
}
```

> **注意**: 当使用 `Task::get()` 同步等待结果时，建议使用 `suspend_for_blocking`，因为调度器模式需要异步执行器配合。

### 4.2 yield - 让出执行

```cpp
Task<void> cooperative_task() {
    for (int i = 0; i < 3; ++i) {
        std::cout << "步骤 " << i << std::endl;
        
        // 让出执行，允许其他协程运行
        co_await yield();
    }
}
```

### 4.3 ConditionVariable - 事件驱动等待（推荐）

`ConditionVariable` 提供真正的事件驱动等待，避免轮询带来的 CPU 浪费：

```cpp
// 基本用法：生产者-消费者模式
std::atomic<bool> data_ready{false};
auto cv = make_condition_variable();

Task<void> consumer_task() {
    std::cout << "等待数据..." << std::endl;
    
    // 事件驱动等待，不消耗 CPU
    co_await cv->wait([&]() { return data_ready.load(); });
    
    std::cout << "数据已就绪!" << std::endl;
}

Task<void> producer_task() {
    co_await suspend_for_blocking(std::chrono::milliseconds{200});
    data_ready = true;
    cv->notify_one();  // 通知等待者
}
```

#### 带超时的等待

```cpp
Task<std::string> wait_with_timeout_demo() {
    auto cv = make_condition_variable();
    bool condition = false;

    // 等待条件，最多 100ms
    bool timed_out = co_await cv->wait_for(
        [&]() { return condition; },
        std::chrono::milliseconds{100});

    if (timed_out) {
        co_return "超时";
    } else {
        co_return "条件满足";
    }
}
```

#### 通知多个等待者

```cpp
auto cv = make_condition_variable();

// 通知一个等待者
cv->notify_one();

// 通知所有等待者
cv->notify_all();
```

### 4.4 wait_until - 轮询式条件等待（已废弃）

> **注意**: `wait_until` 使用轮询方式，会消耗 CPU 资源。推荐使用 `ConditionVariable` 替代。

```cpp
Task<void> wait_for_ready() {
    std::atomic<bool> ready{false};
    
    // 在另一个线程设置 ready
    std::thread worker([&ready]() {
        std::this_thread::sleep_for(std::chrono::milliseconds{500});
        ready = true;
    });
    
    std::cout << "等待就绪标志..." << std::endl;
    
    // 轮询等待条件满足（不推荐，建议使用 ConditionVariable）
    co_await wait_until([&ready]() { return ready.load(); });
    
    std::cout << "就绪!" << std::endl;
    
    worker.join();
}

// 带轮询间隔的版本
Task<void> wait_with_interval() {
    int counter = 0;
    
    // 每 50ms 检查一次条件
    co_await wait_until(
        [&counter]() { return ++counter >= 10; },
        std::chrono::milliseconds{50}
    );
    
    std::cout << "计数器达到: " << counter << std::endl;
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
    std::cout << "立即继续" << std::endl;
}
```

### 4.6 switch_to - 切换执行器

```cpp
Task<void> switch_executor_example() {
    TbbExecutor executor(4);
    
    std::cout << "切换前，线程: " 
              << std::this_thread::get_id() << std::endl;
    
    // 切换到指定执行器
    co_await switch_to(executor);
    
    std::cout << "切换后，线程: " 
              << std::this_thread::get_id() << std::endl;
    
    executor.shutdown();
}

// 延迟切换
Task<void> delayed_switch_example() {
    TbbExecutor executor(4);
    
    // 500ms 后在执行器上恢复
    co_await switch_to_after(executor, std::chrono::milliseconds{500});
    
    std::cout << "延迟后在执行器上恢复" << std::endl;
    
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
    std::cout << "在线程池中运行" << std::endl;
});

// 延迟执行
executor2.execute_after([]() {
    std::cout << "延迟任务" << std::endl;
}, std::chrono::milliseconds{1000});

// 等待所有任务完成
executor2.wait();

// 获取最大并发数
std::cout << "最大并发数: " << executor2.max_concurrency() << std::endl;

// 关闭执行器 (析构时自动调用)
executor2.shutdown();
```

### 5.3 InlineExecutor - 内联执行器

```cpp
// 获取全局内联执行器
auto& executor = inline_executor();

// 任务在当前线程直接执行
executor.execute([]() {
    std::cout << "内联执行" << std::endl;
});

// 延迟执行 (阻塞当前线程)
executor.execute_after([]() {
    std::cout << "延迟后" << std::endl;
}, std::chrono::milliseconds{100});

// 总是返回 true
bool in_thread = executor.is_in_executor_thread();
```

### 5.4 在协程中使用执行器

```cpp
Task<void> executor_usage() {
    TbbExecutor pool(4);
    
    // 在主线程开始
    std::cout << "主线程: " << std::this_thread::get_id() << std::endl;
    
    // 切换到线程池
    co_await switch_to(pool);
    
    std::cout << "线程池线程: " << std::this_thread::get_id() << std::endl;
    
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
    std::cout << "切换前: " << std::this_thread::get_id() << std::endl;
    
    // 切换到调度器管理的默认线程池
    co_await schedule_on_pool();
    
    std::cout << "在线程池中: " << std::this_thread::get_id() << std::endl;
}

Task<void> delayed_pool_task() {
    // 500ms 后在线程池上恢复
    co_await schedule_on_pool_after(std::chrono::milliseconds{500});
    
    std::cout << "延迟后在线程池上恢复" << std::endl;
}
```

### 6.3 Runner - 协程运行器

```cpp
// 运行返回值任务
Task<int> compute() {
    co_return 42;
}

int result = Runner::run(compute());
std::cout << "结果: " << result << std::endl;

// 运行 void 任务
Task<void> work() {
    std::cout << "正在工作..." << std::endl;
    co_return;
}

Runner::run(work());
```

---

## 7. 并行组合 (when_all)

`when_all` 函数允许并行等待多个任务完成，是实现并发的推荐方式。

### 7.1 基本用法

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
    // 并行等待两个任务
    auto [a, b] = co_await when_all(fetch_data_a(), fetch_data_b());
    std::cout << "结果: " << a << ", " << b << std::endl;  // 输出: 结果: 10, 20
}
```

### 7.2 不同类型的任务

`when_all` 支持不同返回类型的任务：

```cpp
Task<int> get_number() {
    co_return 42;
}

Task<std::string> get_string() {
    co_return "hello";
}

Task<void> do_work() {
    std::cout << "工作中..." << std::endl;
    co_return;
}

Task<void> mixed_parallel() {
    // void 任务对应 std::monostate
    auto [num, str, _] = co_await when_all(
        get_number(),
        get_string(),
        do_work()
    );
    std::cout << "数字: " << num << ", 字符串: " << str << std::endl;
}
```

### 7.3 同类型任务向量

对于同类型的多个任务，可以使用 `std::vector`：

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
    
    // 并行执行所有任务
    auto results = co_await when_all(std::move(tasks));
    
    for (int val : results) {
        std::cout << val << " ";  // 输出: 0 10 20 30 40
    }
}
```

### 7.4 异常处理

如果任何任务抛出异常，`when_all` 会捕获第一个异常并重新抛出：

```cpp
Task<int> may_fail(bool fail) {
    if (fail) {
        throw std::runtime_error("任务失败");
    }
    co_return 42;
}

Task<void> handle_errors() {
    try {
        auto [a, b] = co_await when_all(
            may_fail(false),
            may_fail(true)  // 这个会抛出异常
        );
    } catch (const std::exception& e) {
        std::cout << "捕获异常: " << e.what() << std::endl;
    }
}
```

---

## 8. 同步原语

协程模块提供了协程友好的同步原语，避免阻塞线程。

### 8.1 AsyncMutex - 异步互斥锁

`AsyncMutex` 是协程友好的互斥锁，当锁被占用时挂起协程而不是阻塞线程：

```cpp
AsyncMutex mutex;
int shared_counter = 0;

Task<void> increment_counter() {
    // 获取锁
    co_await mutex.lock();
    
    // 临界区
    ++shared_counter;
    
    // 释放锁
    mutex.unlock();
}
```

#### 使用 ScopedLock 自动释放

```cpp
Task<void> safe_increment() {
    // 自动获取和释放锁
    auto guard = co_await mutex.lock_scoped();
    
    // 临界区 - guard 析构时自动释放锁
    ++shared_counter;
    co_await some_async_operation();
    ++shared_counter;
}
```

#### 并发安全示例

```cpp
Task<void> concurrent_updates() {
    AsyncMutex mutex;
    int counter = 0;
    
    // 启动多个并发任务
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
    std::cout << "最终计数: " << counter << std::endl;  // 输出: 1000
}
```

### 8.2 AsyncScope - 异步作用域

`AsyncScope` 用于管理一组并发任务的生命周期，支持 "fire-and-forget" 模式：

```cpp
Task<void> worker_task(int id) {
    std::cout << "Worker " << id << " 开始" << std::endl;
    co_await suspend_for(std::chrono::milliseconds{100});
    std::cout << "Worker " << id << " 完成" << std::endl;
}

Task<void> scope_example() {
    AsyncScope scope;
    
    // 启动多个任务（fire-and-forget）
    scope.spawn(worker_task(1));
    scope.spawn(worker_task(2));
    scope.spawn(worker_task(3));
    
    std::cout << "所有任务已启动" << std::endl;
    
    // 等待所有任务完成
    co_await scope.join();
    
    std::cout << "所有任务已完成" << std::endl;
}
```

#### 结构化并发

`AsyncScope` 确保作用域销毁前所有任务都已结束：

```cpp
Task<void> structured_concurrency() {
    {
        AsyncScope scope;
        
        // 启动后台任务
        scope.spawn(background_work());
        scope.spawn(background_work());
        
        // 执行其他操作
        co_await main_work();
        
        // scope 销毁前会等待所有任务完成
        co_await scope.join();
    }  // 所有任务保证已完成
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
        throw std::runtime_error("任务失败!");
    }
    
    co_return 42;
}

Task<void> caller() {
    try {
        int result = co_await may_fail(true);
        std::cout << "结果: " << result << std::endl;
    } catch (const std::exception& e) {
        std::cout << "捕获到异常: " << e.what() << std::endl;
    }
}

// 使用 get() 时的异常
int main() {
    try {
        auto task = may_fail(true);
        int result = task.get();  // 异常在这里抛出
    } catch (const std::exception& e) {
        std::cout << "错误: " << e.what() << std::endl;
    }
    return 0;
}
```

### 9.2 Generator 中的异常

```cpp
Generator<int> gen_with_error() {
    co_yield 1;
    co_yield 2;
    throw std::runtime_error("生成器错误!");
    co_yield 3;  // 永远不会执行
}

int main() {
    try {
        for (int n : gen_with_error()) {
            std::cout << n << " ";  // 输出: 1 2
        }
    } catch (const std::exception& e) {
        std::cout << "\n错误: " << e.what() << std::endl;
    }
    return 0;
}
```

### 9.3 错误处理模式

```cpp
// 使用 std::optional 进行安全操作
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
        co_return "成功: " + std::to_string(result);
    } catch (const std::exception& e) {
        co_return std::string("错误: ") + e.what();
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

推荐使用 `when_all` 实现并行任务（参见第 7 节）。以下是传统线程方式的示例：

```cpp
Task<int> fetch_from_source_a() {
    co_await suspend_for(std::chrono::milliseconds{100});
    co_return 10;
}

Task<int> fetch_from_source_b() {
    co_await suspend_for(std::chrono::milliseconds{150});
    co_return 20;
}

// 推荐方式：使用 when_all
Task<int> parallel_fetch_recommended() {
    auto [a, b] = co_await when_all(
        fetch_from_source_a(),
        fetch_from_source_b()
    );
    co_return a + b;
}

// 传统方式：使用线程（不推荐）
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
        std::cout << "滴答 " << i << std::endl;
        co_await suspend_for(interval);
    }
    std::cout << "完成!" << std::endl;
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

### Awaitable 函数

| 函数 | 说明 |
|------|------|
| `suspend_for(duration)` | 延时等待（调度器模式，不阻塞线程） |
| `suspend_for_blocking(duration)` | 延时等待（阻塞模式，与 Task::get() 兼容） |
| `suspend_for_seconds(secs)` | 延时指定秒数（阻塞模式） |
| `yield()` | 让出执行 |
| `make_condition_variable()` | 创建条件变量 |
| `cv->wait(pred)` | 事件驱动等待条件满足 |
| `cv->wait_for(pred, timeout)` | 带超时的事件驱动等待 |
| `cv->notify_one()` | 通知一个等待者 |
| `cv->notify_all()` | 通知所有等待者 |
| `wait_until(pred)` | 轮询等待条件满足（已废弃） |
| `wait_until(pred, interval)` | 带轮询间隔等待（已废弃） |
| `ready(value)` | 立即返回值 |
| `ready()` | 立即返回 void |
| `switch_to(executor)` | 切换到执行器 |
| `switch_to_after(executor, delay)` | 延迟切换到执行器 |
| `schedule_on_pool()` | 切换到默认线程池 |
| `schedule_on_pool_after(delay)` | 延迟切换到线程池 |

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

| 方法 | 说明 |
|------|------|
| `run(Task<T>)` | 阻塞运行任务，返回结果 |
| `run(Task<void>)` | 阻塞运行 void 任务 |
| `spawn(Task<T>&&)` | 启动任务到线程池（不等待完成） |

### when_all

| 函数 | 说明 |
|------|------|
| `when_all(Tasks...)` | 并行等待多个不同类型任务 |
| `when_all(vector<Task<T>>)` | 并行等待同类型任务向量 |

### AsyncMutex

| 方法 | 说明 |
|------|------|
| `lock()` | 获取锁（返回 Awaitable） |
| `lock_scoped()` | 获取锁并返回 ScopedLock |
| `unlock()` | 释放锁 |

### AsyncScope

| 方法 | 说明 |
|------|------|
| `spawn(Task<T>&&)` | 启动任务（fire-and-forget） |
| `join()` | 等待所有任务完成（返回 Awaitable） |

---

## 参考资料

- [C++20 协程规范](https://en.cppreference.com/w/cpp/language/coroutines)
- [Corona 协程设计文档](coroutine_design.md)
- [示例代码](../../examples/10_coroutine/main.cpp)
