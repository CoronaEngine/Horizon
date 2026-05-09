# Corona Framework 协程封装设计文档

## 1. 概述

本文档描述 Corona Framework 中 **C++20 协程（Coroutine）** 封装模块的设计方案。协程是现代 C++ 中处理异步操作的核心机制，通过封装标准库协程原语，提供简洁、类型安全且高性能的异步编程接口。

### 1.1 设计目标

| 目标 | 说明 |
|------|------|
| **零开销抽象** | 编译期多态，无虚函数开销 |
| **类型安全** | 利用 C++20 concepts 确保编译期类型检查 |
| **易于使用** | 简洁的 API，隐藏协程底层复杂性 |
| **灵活调度** | 支持自定义执行器和调度策略 |
| **异常安全** | 正确传播异常，支持 RAII |
| **可组合性** | 支持协程嵌套、并发组合（all/any/race） |

### 1.2 适用场景

- **异步 I/O**：文件读写、网络请求
- **游戏逻辑**：动画序列、AI 行为树、对话系统
- **资源加载**：异步加载纹理、模型、音频
- **定时任务**：延时执行、周期性任务
- **生成器**：惰性序列生成、数据流处理

### 1.3 核心概念

| 术语 | 说明 |
|------|------|
| **Coroutine** | 可暂停/恢复执行的函数 |
| **Promise** | 协程状态管理器，控制协程行为 |
| **Awaitable** | 可被 `co_await` 的对象 |
| **Awaiter** | 实现 `await_ready/suspend/resume` 三件套的类型 |
| **Task** | 表示异步操作结果的协程返回类型 |
| **Generator** | 惰性生成值序列的协程返回类型 |

---

## 2. 架构设计

### 2.1 整体架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Coroutine Module                             │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │                      High-Level API                             │ │
│  ├────────────────┬───────────────┬───────────────┬───────────────┤ │
│  │   Task<T>      │  Generator<T> │  AsyncScope   │  when_all()   │ │
│  │  (异步任务)     │  (生成器)      │  (作用域管理)  │  when_any()   │ │
│  └────────────────┴───────────────┴───────────────┴───────────────┘ │
│                              │                                       │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │                      Awaitable Utilities                        │ │
│  ├────────────────┬───────────────┬───────────────┬───────────────┤ │
│  │  suspend_for() │  yield_value  │  switch_to()  │  on_executor()│ │
│  │  (延时等待)     │  (生成值)      │  (切换线程)    │  (指定执行器)  │ │
│  └────────────────┴───────────────┴───────────────┴───────────────┘ │
│                              │                                       │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │                      Core Infrastructure                        │ │
│  ├────────────────┬───────────────┬───────────────┬───────────────┤ │
│  │  Promise Types │  Awaiter Base │  Handle Mgmt  │  Concepts     │ │
│  │  (Promise 实现) │  (等待器基类)  │  (句柄管理)    │  (约束定义)    │ │
│  └────────────────┴───────────────┴───────────────┴───────────────┘ │
│                              │                                       │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │                      Executor Layer                             │ │
│  ├────────────────┬───────────────┬───────────────────────────────┤ │
│  │  IExecutor     │  ThreadPool   │  InlineExecutor               │ │
│  │  (执行器接口)   │  (线程池)      │  (内联执行)                    │ │
│  └────────────────┴───────────────┴───────────────────────────────┘ │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
              ┌───────────────────────────────────┐
              │     C++20 Standard Library        │
              │  <coroutine> / std::coroutine_*   │
              └───────────────────────────────────┘
```

### 2.2 模块依赖

```
┌─────────────────────────────────────────────────────────┐
│                    Coroutine Module                      │
│                   (独立模块，无框架依赖)                   │
├─────────────────────────────────────────────────────────┤
│  Task<T>  │  Generator<T>  │  Executors  │  Combinators │
└─────────────────────────────────────────────────────────┘
                          │
          ┌───────────────┼───────────────┐
          ▼               ▼               ▼
    ┌──────────┐    ┌──────────┐    ┌──────────────┐
    │   TBB    │    │  Quill   │    │  C++20 STL   │
    │ (线程池) │    │ (日志)    │    │ <coroutine>  │
    └──────────┘    └──────────┘    └──────────────┘
```

> **设计原则**：协程模块保持完全独立，不依赖 Corona Framework 的其他模块（如 EventStream、SystemBase 等）。仅依赖：
> - **TBB**：用于 `ThreadPoolExecutor` 的任务调度
> - **Quill**：用于协程调试日志（可选）
> - **C++20 标准库**：`<coroutine>`, `<atomic>`, `<thread>` 等

---

## 3. 核心数据结构

### 3.1 协程概念约束

```cpp
namespace Corona::Kernel::Coro {

/// Awaitable 概念：可被 co_await 的类型
template <typename T>
concept Awaitable = requires(T t) {
    { t.await_ready() } -> std::convertible_to<bool>;
    { t.await_suspend(std::coroutine_handle<>{}) };
    { t.await_resume() };
};

/// Awaiter 概念：更严格的等待器约束
template <typename T, typename Result = void>
concept Awaiter = Awaitable<T> && requires(T t) {
    { t.await_resume() } -> std::convertible_to<Result>;
};

/// Promise 概念：协程承诺类型约束
template <typename P>
concept PromiseType = requires(P p) {
    { p.get_return_object() };
    { p.initial_suspend() } -> Awaitable;
    { p.final_suspend() } noexcept -> Awaitable;
    { p.unhandled_exception() };
};

/// 执行器概念
template <typename E>
concept Executor = requires(E e, std::function<void()> f) {
    { e.execute(std::move(f)) };
    { e.execute_after(std::move(f), std::chrono::milliseconds{}) };
};

} // namespace Corona::Kernel::Coro
```

### 3.2 Task<T> - 异步任务

`Task<T>` 是最常用的协程返回类型，表示一个最终会产生 `T` 类型值的异步操作。

```cpp
namespace Corona::Kernel::Coro {

/// 任务状态枚举
enum class TaskState : uint8_t {
    Pending,    ///< 等待执行
    Running,    ///< 正在执行
    Completed,  ///< 执行完成
    Failed      ///< 执行失败
};

/// 异步任务类型
template <typename T = void>
class Task {
public:
    /// Promise 类型定义
    struct promise_type {
        /// 协程开始时的行为：惰性启动（lazy）
        std::suspend_always initial_suspend() noexcept { return {}; }
        
        /// 协程结束时的行为：挂起以便调用者获取结果
        auto final_suspend() noexcept { return FinalAwaiter{}; }
        
        /// 获取返回对象
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        /// 设置返回值
        template <typename U>
            requires std::convertible_to<U, T>
        void return_value(U&& value) {
            result_.emplace(std::forward<U>(value));
        }
        
        /// 处理未捕获异常
        void unhandled_exception() {
            exception_ = std::current_exception();
        }
        
        /// 获取结果（可能抛出异常）
        T& result() & {
            if (exception_) {
                std::rethrow_exception(exception_);
            }
            return *result_;
        }
        
        T&& result() && {
            if (exception_) {
                std::rethrow_exception(exception_);
            }
            return std::move(*result_);
        }
        
        /// 设置等待此任务完成的协程句柄
        void set_continuation(std::coroutine_handle<> cont) noexcept {
            continuation_ = cont;
        }
        
    private:
        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            
            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> h) noexcept {
                // 对称转移：直接跳转到等待者，避免栈溢出
                auto& promise = h.promise();
                if (promise.continuation_) {
                    return promise.continuation_;
                }
                return std::noop_coroutine();
            }
            
            void await_resume() noexcept {}
        };
        
        std::optional<T> result_;
        std::exception_ptr exception_;
        std::coroutine_handle<> continuation_;
    };
    
    // ========================================
    // 构造与析构
    // ========================================
    
    Task() noexcept = default;
    
    explicit Task(std::coroutine_handle<promise_type> h) noexcept 
        : handle_(h) {}
    
    ~Task() {
        if (handle_) {
            handle_.destroy();
        }
    }
    
    // 禁止拷贝
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    // 支持移动
    Task(Task&& other) noexcept 
        : handle_(std::exchange(other.handle_, nullptr)) {}
    
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    
    // ========================================
    // Awaitable 接口
    // ========================================
    
    bool await_ready() const noexcept {
        return handle_.done();
    }
    
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        handle_.promise().set_continuation(awaiting);
        return handle_;  // 对称转移到此任务
    }
    
    T await_resume() {
        return std::move(handle_.promise()).result();
    }
    
    // ========================================
    // 同步等待
    // ========================================
    
    /// 阻塞等待任务完成并获取结果
    T get() {
        // 简单实现：循环恢复直到完成
        while (!handle_.done()) {
            handle_.resume();
        }
        return std::move(handle_.promise()).result();
    }
    
    /// 检查任务是否完成
    [[nodiscard]] bool done() const noexcept {
        return handle_ && handle_.done();
    }
    
    /// 启动任务执行
    void resume() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }
    
private:
    std::coroutine_handle<promise_type> handle_;
};

/// Task<void> 特化
template <>
class Task<void> {
public:
    struct promise_type {
        std::suspend_always initial_suspend() noexcept { return {}; }
        auto final_suspend() noexcept { return FinalAwaiter{}; }
        
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        void return_void() noexcept {}
        
        void unhandled_exception() {
            exception_ = std::current_exception();
        }
        
        void result() {
            if (exception_) {
                std::rethrow_exception(exception_);
            }
        }
        
        void set_continuation(std::coroutine_handle<> cont) noexcept {
            continuation_ = cont;
        }
        
    private:
        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> h) noexcept {
                auto& promise = h.promise();
                return promise.continuation_ ? promise.continuation_ 
                                             : std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };
        
        std::exception_ptr exception_;
        std::coroutine_handle<> continuation_;
    };
    
    Task() noexcept = default;
    explicit Task(std::coroutine_handle<promise_type> h) noexcept : handle_(h) {}
    ~Task() { if (handle_) handle_.destroy(); }
    
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    
    bool await_ready() const noexcept { return handle_.done(); }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        handle_.promise().set_continuation(awaiting);
        return handle_;
    }
    void await_resume() { handle_.promise().result(); }
    
    void get() {
        while (!handle_.done()) handle_.resume();
        handle_.promise().result();
    }
    
    [[nodiscard]] bool done() const noexcept { return handle_ && handle_.done(); }
    void resume() { if (handle_ && !handle_.done()) handle_.resume(); }
    
private:
    std::coroutine_handle<promise_type> handle_;
};

} // namespace Corona::Kernel::Coro
```

### 3.3 Generator<T> - 生成器

`Generator<T>` 用于惰性生成值序列，支持 `co_yield` 语法。

```cpp
namespace Corona::Kernel::Coro {

/// 生成器类型
template <typename T>
class Generator {
public:
    struct promise_type {
        T current_value_;
        std::exception_ptr exception_;
        
        Generator get_return_object() {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        /// 惰性启动：创建后不立即执行
        std::suspend_always initial_suspend() noexcept { return {}; }
        
        /// 完成后挂起
        std::suspend_always final_suspend() noexcept { return {}; }
        
        /// yield 值
        std::suspend_always yield_value(T value) noexcept(
            std::is_nothrow_move_constructible_v<T>) {
            current_value_ = std::move(value);
            return {};
        }
        
        /// 生成器不应该 return 值
        void return_void() noexcept {}
        
        void unhandled_exception() {
            exception_ = std::current_exception();
        }
        
        void rethrow_if_exception() {
            if (exception_) {
                std::rethrow_exception(exception_);
            }
        }
    };
    
    // ========================================
    // 迭代器支持
    // ========================================
    
    class Iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using reference = T&;
        using pointer = T*;
        
        Iterator() noexcept = default;
        
        explicit Iterator(std::coroutine_handle<promise_type> handle) noexcept
            : handle_(handle) {}
        
        Iterator& operator++() {
            handle_.resume();
            if (handle_.done()) {
                handle_.promise().rethrow_if_exception();
                handle_ = nullptr;
            }
            return *this;
        }
        
        void operator++(int) { ++*this; }
        
        [[nodiscard]] reference operator*() const {
            return handle_.promise().current_value_;
        }
        
        [[nodiscard]] pointer operator->() const {
            return std::addressof(handle_.promise().current_value_);
        }
        
        [[nodiscard]] bool operator==(const Iterator& other) const noexcept {
            return handle_ == other.handle_;
        }
        
        [[nodiscard]] bool operator!=(const Iterator& other) const noexcept {
            return !(*this == other);
        }
        
    private:
        std::coroutine_handle<promise_type> handle_;
    };
    
    // ========================================
    // 构造与析构
    // ========================================
    
    Generator() noexcept = default;
    
    explicit Generator(std::coroutine_handle<promise_type> h) noexcept 
        : handle_(h) {}
    
    ~Generator() {
        if (handle_) {
            handle_.destroy();
        }
    }
    
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;
    
    Generator(Generator&& other) noexcept 
        : handle_(std::exchange(other.handle_, nullptr)) {}
    
    Generator& operator=(Generator&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    
    // ========================================
    // 范围接口
    // ========================================
    
    [[nodiscard]] Iterator begin() {
        if (handle_) {
            handle_.resume();
            if (handle_.done()) {
                handle_.promise().rethrow_if_exception();
                return Iterator{};
            }
        }
        return Iterator{handle_};
    }
    
    [[nodiscard]] Iterator end() noexcept {
        return Iterator{};
    }
    
    /// 获取下一个值（可选返回）
    [[nodiscard]] std::optional<T> next() {
        if (!handle_ || handle_.done()) {
            return std::nullopt;
        }
        handle_.resume();
        if (handle_.done()) {
            handle_.promise().rethrow_if_exception();
            return std::nullopt;
        }
        return handle_.promise().current_value_;
    }
    
private:
    std::coroutine_handle<promise_type> handle_;
};

} // namespace Corona::Kernel::Coro
```

---

## 4. Awaitable 工具类

### 4.1 延时等待

```cpp
namespace Corona::Kernel::Coro {

/// 延时等待器
class SuspendFor {
public:
    explicit SuspendFor(std::chrono::milliseconds duration) noexcept
        : duration_(duration) {}
    
    bool await_ready() const noexcept {
        return duration_.count() <= 0;
    }
    
    void await_suspend(std::coroutine_handle<> handle) const {
        // 实际实现中应该使用定时器调度器
        // 这里使用简单的 sleep 作为示例
        std::this_thread::sleep_for(duration_);
        handle.resume();
    }
    
    void await_resume() const noexcept {}
    
private:
    std::chrono::milliseconds duration_;
};

/// 便捷函数：延时指定时间
[[nodiscard]] inline SuspendFor suspend_for(std::chrono::milliseconds duration) {
    return SuspendFor{duration};
}

/// 便捷函数：延时指定秒数
[[nodiscard]] inline SuspendFor suspend_for_seconds(double seconds) {
    return SuspendFor{std::chrono::milliseconds{
        static_cast<long long>(seconds * 1000)}};
}

} // namespace Corona::Kernel::Coro
```

### 4.2 线程切换

```cpp
namespace Corona::Kernel::Coro {

/// 切换到指定执行器
template <Executor E>
class SwitchToExecutor {
public:
    explicit SwitchToExecutor(E& executor) noexcept : executor_(executor) {}
    
    bool await_ready() const noexcept { return false; }
    
    void await_suspend(std::coroutine_handle<> handle) const {
        executor_.execute([handle]() mutable {
            handle.resume();
        });
    }
    
    void await_resume() const noexcept {}
    
private:
    E& executor_;
};

/// 便捷函数：切换到指定执行器
template <Executor E>
[[nodiscard]] SwitchToExecutor<E> switch_to(E& executor) {
    return SwitchToExecutor<E>{executor};
}

/// 让出当前时间片（切换到其他协程）
struct Yield {
    bool await_ready() const noexcept { return false; }
    
    void await_suspend(std::coroutine_handle<> handle) const noexcept {
        // 立即重新调度
        handle.resume();
    }
    
    void await_resume() const noexcept {}
};

/// 便捷函数：让出执行
[[nodiscard]] inline Yield yield() noexcept {
    return Yield{};
}

} // namespace Corona::Kernel::Coro
```

### 4.3 条件等待

```cpp
namespace Corona::Kernel::Coro {

/// 等待条件满足
template <typename Predicate>
    requires std::invocable<Predicate> && 
             std::convertible_to<std::invoke_result_t<Predicate>, bool>
class WaitUntil {
public:
    explicit WaitUntil(Predicate pred, std::chrono::milliseconds poll_interval = 
                       std::chrono::milliseconds{10})
        : predicate_(std::move(pred)), poll_interval_(poll_interval) {}
    
    bool await_ready() const {
        return predicate_();
    }
    
    void await_suspend(std::coroutine_handle<> handle) const {
        // 轮询直到条件满足
        while (!predicate_()) {
            std::this_thread::sleep_for(poll_interval_);
        }
        handle.resume();
    }
    
    void await_resume() const noexcept {}
    
private:
    Predicate predicate_;
    std::chrono::milliseconds poll_interval_;
};

/// 便捷函数：等待条件满足
template <typename Predicate>
[[nodiscard]] WaitUntil<Predicate> wait_until(Predicate&& pred) {
    return WaitUntil<Predicate>{std::forward<Predicate>(pred)};
}

} // namespace Corona::Kernel::Coro
```

---

## 5. 并发组合器

### 5.1 when_all - 等待所有任务完成

```cpp
namespace Corona::Kernel::Coro {

/// 等待所有任务完成
template <typename... Tasks>
class WhenAll {
public:
    explicit WhenAll(Tasks&&... tasks) 
        : tasks_(std::forward<Tasks>(tasks)...) {}
    
    bool await_ready() const noexcept {
        return std::apply([](const auto&... t) {
            return (t.done() && ...);
        }, tasks_);
    }
    
    void await_suspend(std::coroutine_handle<> handle) {
        // 启动所有任务并等待完成
        remaining_ = sizeof...(Tasks);
        continuation_ = handle;
        
        std::apply([this](auto&... t) {
            (start_task(t), ...);
        }, tasks_);
    }
    
    auto await_resume() {
        // 收集所有结果
        return std::apply([](auto&... t) {
            return std::make_tuple(t.await_resume()...);
        }, tasks_);
    }
    
private:
    template <typename T>
    void start_task(T& task) {
        // 实际实现需要更复杂的调度逻辑
        // 这里简化为串行执行
        while (!task.done()) {
            task.resume();
        }
        if (--remaining_ == 0 && continuation_) {
            continuation_.resume();
        }
    }
    
    std::tuple<Tasks...> tasks_;
    std::atomic<std::size_t> remaining_{0};
    std::coroutine_handle<> continuation_;
};

/// 便捷函数：等待所有任务
template <typename... Tasks>
[[nodiscard]] auto when_all(Tasks&&... tasks) {
    return WhenAll<Tasks...>{std::forward<Tasks>(tasks)...};
}

} // namespace Corona::Kernel::Coro
```

### 5.2 when_any - 等待任一任务完成

```cpp
namespace Corona::Kernel::Coro {

/// 等待任一任务完成的结果
template <typename T>
struct WhenAnyResult {
    std::size_t index;      ///< 完成的任务索引
    T value;                ///< 结果值
};

/// 等待任一任务完成
template <typename T>
class WhenAny {
public:
    explicit WhenAny(std::vector<Task<T>> tasks) 
        : tasks_(std::move(tasks)) {}
    
    bool await_ready() const noexcept {
        for (std::size_t i = 0; i < tasks_.size(); ++i) {
            if (tasks_[i].done()) {
                completed_index_ = i;
                return true;
            }
        }
        return false;
    }
    
    void await_suspend(std::coroutine_handle<> handle) {
        continuation_ = handle;
        
        // 轮询检查哪个任务先完成
        // 实际实现应使用事件驱动
        while (true) {
            for (std::size_t i = 0; i < tasks_.size(); ++i) {
                if (!tasks_[i].done()) {
                    tasks_[i].resume();
                    if (tasks_[i].done()) {
                        completed_index_ = i;
                        handle.resume();
                        return;
                    }
                }
            }
        }
    }
    
    WhenAnyResult<T> await_resume() {
        return WhenAnyResult<T>{
            .index = completed_index_,
            .value = tasks_[completed_index_].await_resume()
        };
    }
    
private:
    std::vector<Task<T>> tasks_;
    mutable std::size_t completed_index_ = 0;
    std::coroutine_handle<> continuation_;
};

/// 便捷函数：等待任一任务
template <typename T>
[[nodiscard]] WhenAny<T> when_any(std::vector<Task<T>> tasks) {
    return WhenAny<T>{std::move(tasks)};
}

} // namespace Corona::Kernel::Coro
```

---

## 6. 执行器接口

### 6.1 执行器抽象

```cpp
namespace Corona::Kernel::Coro {

/// 执行器接口
class IExecutor {
public:
    virtual ~IExecutor() = default;
    
    /// 提交任务立即执行
    virtual void execute(std::function<void()> task) = 0;
    
    /// 延迟执行任务
    virtual void execute_after(std::function<void()> task, 
                               std::chrono::milliseconds delay) = 0;
    
    /// 检查是否在此执行器的线程上
    [[nodiscard]] virtual bool is_in_executor_thread() const = 0;
};

/// 内联执行器（在当前线程直接执行）
class InlineExecutor : public IExecutor {
public:
    void execute(std::function<void()> task) override {
        task();
    }
    
    void execute_after(std::function<void()> task, 
                       std::chrono::milliseconds delay) override {
        std::this_thread::sleep_for(delay);
        task();
    }
    
    [[nodiscard]] bool is_in_executor_thread() const override {
        return true;
    }
};

/// 线程池执行器
class ThreadPoolExecutor : public IExecutor {
public:
    explicit ThreadPoolExecutor(std::size_t num_threads = 
                                std::thread::hardware_concurrency());
    ~ThreadPoolExecutor() override;
    
    void execute(std::function<void()> task) override;
    void execute_after(std::function<void()> task, 
                       std::chrono::milliseconds delay) override;
    [[nodiscard]] bool is_in_executor_thread() const override;
    
    /// 停止执行器
    void shutdown();
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Corona::Kernel::Coro
```

---

## 7. 线程池执行器实现（基于 TBB）

### 7.1 TBB 线程池执行器

```cpp
#include <tbb/task_arena.h>
#include <tbb/task_group.h>
#include <tbb/concurrent_queue.h>
#include <atomic>
#include <thread>
#include <chrono>

namespace Corona::Kernel::Coro {

/// 基于 TBB 的线程池执行器
class TbbExecutor : public IExecutor {
public:
    /// 使用默认线程数构造
    TbbExecutor() 
        : arena_(tbb::task_arena::automatic)
        , running_(true) {
        // 启动定时任务处理线程
        timer_thread_ = std::thread([this]() { timer_loop(); });
    }
    
    /// 指定线程数构造
    explicit TbbExecutor(int num_threads) 
        : arena_(num_threads)
        , running_(true) {
        timer_thread_ = std::thread([this]() { timer_loop(); });
    }
    
    ~TbbExecutor() override {
        shutdown();
    }
    
    /// 提交任务到 TBB 线程池
    void execute(std::function<void()> task) override {
        arena_.execute([this, task = std::move(task)]() {
            task_group_.run(task);
        });
    }
    
    /// 延迟执行任务
    void execute_after(std::function<void()> task, 
                       std::chrono::milliseconds delay) override {
        auto deadline = std::chrono::steady_clock::now() + delay;
        delayed_tasks_.push({std::move(task), deadline});
    }
    
    /// 检查是否在执行器线程中
    [[nodiscard]] bool is_in_executor_thread() const override {
        return tbb::this_task_arena::current_thread_index() >= 0;
    }
    
    /// 等待所有任务完成
    void wait() {
        arena_.execute([this]() {
            task_group_.wait();
        });
    }
    
    /// 关闭执行器
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
    
private:
    struct DelayedTask {
        std::function<void()> task;
        std::chrono::steady_clock::time_point deadline;
    };
    
    void timer_loop() {
        while (running_) {
            DelayedTask delayed;
            while (delayed_tasks_.try_pop(delayed)) {
                auto now = std::chrono::steady_clock::now();
                if (now >= delayed.deadline) {
                    execute(std::move(delayed.task));
                } else {
                    // 重新入队
                    delayed_tasks_.push(std::move(delayed));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    
    tbb::task_arena arena_;
    tbb::task_group task_group_;
    tbb::concurrent_queue<DelayedTask> delayed_tasks_;
    std::thread timer_thread_;
    std::atomic<bool> running_;
};

} // namespace Corona::Kernel::Coro
```

### 7.2 协程调度器

```cpp
namespace Corona::Kernel::Coro {

/// 全局协程调度器
class Scheduler {
public:
    /// 获取单例实例
    static Scheduler& instance() {
        static Scheduler s;
        return s;
    }
    
    /// 设置默认执行器
    void set_default_executor(std::shared_ptr<IExecutor> executor) {
        default_executor_ = std::move(executor);
    }
    
    /// 获取默认执行器
    [[nodiscard]] IExecutor& default_executor() {
        if (!default_executor_) {
            // 惰性初始化 TBB 执行器
            default_executor_ = std::make_shared<TbbExecutor>();
        }
        return *default_executor_;
    }
    
    /// 调度协程到默认执行器
    void schedule(std::coroutine_handle<> handle) {
        default_executor().execute([handle]() mutable {
            handle.resume();
        });
    }
    
    /// 延迟调度协程
    void schedule_after(std::coroutine_handle<> handle, 
                        std::chrono::milliseconds delay) {
        default_executor().execute_after([handle]() mutable {
            handle.resume();
        }, delay);
    }
    
private:
    Scheduler() = default;
    std::shared_ptr<IExecutor> default_executor_;
};

/// 切换到调度器管理的线程池
struct ScheduleOn {
    bool await_ready() const noexcept { return false; }
    
    void await_suspend(std::coroutine_handle<> handle) const {
        Scheduler::instance().schedule(handle);
    }
    
    void await_resume() const noexcept {}
};

/// 便捷函数：切换到线程池执行
[[nodiscard]] inline ScheduleOn schedule_on_pool() {
    return ScheduleOn{};
}

} // namespace Corona::Kernel::Coro
```

---

## 8. 使用示例

### 8.1 基本任务

```cpp
#include <corona/kernel/coro/task.h>
#include <iostream>

using namespace Corona::Kernel::Coro;

// 简单异步函数
Task<int> async_add(int a, int b) {
    co_await suspend_for(std::chrono::milliseconds{100});
    co_return a + b;
}

// 组合多个异步操作
Task<int> compute() {
    int x = co_await async_add(1, 2);
    int y = co_await async_add(3, 4);
    co_return x + y;
}

int main() {
    auto task = compute();
    int result = task.get();
    std::cout << "Result: " << result << std::endl;  // 输出: Result: 10
    return 0;
}
```

### 8.2 生成器

```cpp
#include <corona/kernel/coro/generator.h>
#include <iostream>

using namespace Corona::Kernel::Coro;

// 斐波那契数列生成器
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
    for (int n : fibonacci(10)) {
        std::cout << n << " ";  // 输出: 0 1 1 2 3 5 8 13 21 34
    }
    std::cout << std::endl;
    return 0;
}
```

### 8.3 并发任务示例

```cpp
#include <corona/kernel/coro/coro.h>
#include <iostream>
#include <vector>

using namespace Corona::Kernel::Coro;

// 模拟异步网络请求
Task<std::string> fetch_url(std::string url) {
    co_await schedule_on_pool();  // 切换到线程池
    co_await suspend_for(std::chrono::milliseconds{100});  // 模拟延迟
    co_return "Response from: " + url;
}

// 模拟异步文件读取
Task<std::vector<char>> read_file_async(std::string path) {
    co_await schedule_on_pool();
    co_await suspend_for(std::chrono::milliseconds{50});
    co_return std::vector<char>{'H', 'e', 'l', 'l', 'o'};
}

// 并发执行多个任务
Task<void> parallel_example() {
    // 同时启动多个请求
    auto [r1, r2, r3] = co_await when_all(
        fetch_url("https://api.example.com/a"),
        fetch_url("https://api.example.com/b"),
        fetch_url("https://api.example.com/c")
    );
    
    std::cout << r1 << std::endl;
    std::cout << r2 << std::endl;
    std::cout << r3 << std::endl;
}

// 超时控制示例
Task<std::optional<std::string>> fetch_with_timeout(
    std::string url, 
    std::chrono::milliseconds timeout) 
{
    // 使用 when_any 实现超时
    std::vector<Task<std::string>> tasks;
    tasks.push_back(fetch_url(std::move(url)));
    
    auto result = co_await when_any(std::move(tasks));
    co_return result.value;
}

int main() {
    auto task = parallel_example();
    task.get();
    return 0;
}
```

### 8.4 协程运行器

```cpp
#include <corona/kernel/coro/coro.h>

using namespace Corona::Kernel::Coro;

/// 简单的协程运行器（用于主函数）
class CoroRunner {
public:
    /// 阻塞运行一个任务直到完成
    template <typename T>
    static T run(Task<T> task) {
        return task.get();
    }
    
    /// 阻塞运行 void 任务
    static void run(Task<void> task) {
        task.get();
    }
    
    /// 启动任务并返回（不等待完成）
    template <typename T>
    static void spawn(Task<T> task) {
        Scheduler::instance().schedule([t = std::move(task)]() mutable {
            while (!t.done()) {
                t.resume();
            }
        });
    }
    
    /// 运行多个任务直到全部完成
    template <typename... Tasks>
    static void run_all(Tasks&&... tasks) {
        auto combined = when_all(std::forward<Tasks>(tasks)...);
        run(std::move(combined));
    }
};

// 使用示例
int main() {
    // 方式 1：直接运行
    int result = CoroRunner::run(async_add(1, 2));
    
    // 方式 2：后台启动
    CoroRunner::spawn(some_background_task());
    
    // 方式 3：并行运行多个
    CoroRunner::run_all(
        task1(),
        task2(),
        task3()
    );
    
    return 0;
}
```

---

## 9. 性能考量

### 9.1 内存开销

| 组件 | 大小 | 说明 |
|------|------|------|
| `Task<T>` | 8 bytes | 只存储协程句柄 |
| `Generator<T>` | 8 bytes | 只存储协程句柄 |
| Promise 帧 | ~64-128 bytes | 取决于局部变量 |
| Awaiter | ~16-32 bytes | 取决于具体类型 |

### 9.2 优化建议

1. **避免频繁分配**：复用协程帧，使用对象池
2. **对称转移**：使用 `std::coroutine_handle<>::resume()` 避免栈溢出
3. **减少挂起点**：合并连续的 `co_await`
4. **内联小任务**：对于简单操作避免使用协程
5. **缓存局部性**：将相关协程数据放在一起

### 9.3 调试支持

```cpp
namespace Corona::Kernel::Coro {

/// 协程调试工具
class CoroutineDebugger {
public:
    /// 注册协程创建
    static void on_create(std::coroutine_handle<> h, std::string_view name);
    
    /// 注册协程销毁
    static void on_destroy(std::coroutine_handle<> h);
    
    /// 注册协程挂起
    static void on_suspend(std::coroutine_handle<> h);
    
    /// 注册协程恢复
    static void on_resume(std::coroutine_handle<> h);
    
    /// 打印所有活跃协程
    static void dump_active_coroutines();
    
    /// 获取协程调用栈
    static std::vector<std::string> get_coroutine_stack(std::coroutine_handle<> h);
};

} // namespace Corona::Kernel::Coro
```

---

## 10. 文件结构

```
include/corona/kernel/coro/
├── coro_concepts.h      # 协程概念定义
├── task.h               # Task<T> 实现
├── generator.h          # Generator<T> 实现
├── awaitables.h         # 等待器工具类
├── combinators.h        # when_all, when_any 等
├── executor.h           # 执行器接口
├── tbb_executor.h       # TBB 线程池执行器
├── scheduler.h          # 协程调度器
├── runner.h             # 协程运行器
└── coro.h               # 统一头文件

src/kernel/coro/
├── CMakeLists.txt
├── tbb_executor.cpp     # TBB 执行器实现
├── scheduler.cpp        # 调度器实现
└── debugger.cpp         # 调试工具实现（依赖 Quill）

tests/kernel/
├── coro_task_test.cpp       # Task 单元测试
├── coro_generator_test.cpp  # Generator 单元测试
├── coro_executor_test.cpp   # 执行器测试
├── coro_mt_test.cpp         # 多线程协程测试
└── coro_benchmark_test.cpp  # 性能基准测试
```

### 10.1 CMakeLists.txt 示例

```cmake
# src/kernel/coro/CMakeLists.txt

# 协程模块源文件
set(CORONA_CORO_SOURCES
    tbb_executor.cpp
    scheduler.cpp
    debugger.cpp
)

# 协程模块头文件
set(CORONA_CORO_HEADERS
    ${CMAKE_SOURCE_DIR}/include/corona/kernel/coro/coro_concepts.h
    ${CMAKE_SOURCE_DIR}/include/corona/kernel/coro/task.h
    ${CMAKE_SOURCE_DIR}/include/corona/kernel/coro/generator.h
    ${CMAKE_SOURCE_DIR}/include/corona/kernel/coro/awaitables.h
    ${CMAKE_SOURCE_DIR}/include/corona/kernel/coro/combinators.h
    ${CMAKE_SOURCE_DIR}/include/corona/kernel/coro/executor.h
    ${CMAKE_SOURCE_DIR}/include/corona/kernel/coro/tbb_executor.h
    ${CMAKE_SOURCE_DIR}/include/corona/kernel/coro/scheduler.h
    ${CMAKE_SOURCE_DIR}/include/corona/kernel/coro/runner.h
    ${CMAKE_SOURCE_DIR}/include/corona/kernel/coro/coro.h
)

# 添加到 kernel 库
target_sources(corona_kernel PRIVATE
    ${CORONA_CORO_SOURCES}
    ${CORONA_CORO_HEADERS}
)

# 依赖 TBB 和 Quill（已在父 CMakeLists.txt 中链接）
```

---

## 11. 调试支持（基于 Quill）

### 11.1 协程调试器

```cpp
#include <quill/Quill.h>
#include <unordered_map>
#include <mutex>
#include <atomic>

namespace Corona::Kernel::Coro {

/// 协程状态追踪
struct CoroutineInfo {
    std::string name;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point last_resumed;
    std::size_t suspend_count = 0;
    std::size_t resume_count = 0;
    bool is_done = false;
};

/// 协程调试工具（可选，需要启用宏 CORONA_CORO_DEBUG）
class CoroutineDebugger {
public:
    static CoroutineDebugger& instance() {
        static CoroutineDebugger d;
        return d;
    }
    
    /// 注册协程创建
    void on_create(void* handle, std::string_view name) {
#ifdef CORONA_CORO_DEBUG
        std::lock_guard lock(mutex_);
        auto& info = coroutines_[handle];
        info.name = std::string(name);
        info.created_at = std::chrono::steady_clock::now();
        LOG_DEBUG(logger_, "Coroutine created: {} ({})", name, handle);
        ++total_created_;
#endif
    }
    
    /// 注册协程销毁
    void on_destroy(void* handle) {
#ifdef CORONA_CORO_DEBUG
        std::lock_guard lock(mutex_);
        if (auto it = coroutines_.find(handle); it != coroutines_.end()) {
            LOG_DEBUG(logger_, "Coroutine destroyed: {} ({})", 
                      it->second.name, handle);
            coroutines_.erase(it);
        }
        ++total_destroyed_;
#endif
    }
    
    /// 注册协程挂起
    void on_suspend(void* handle) {
#ifdef CORONA_CORO_DEBUG
        std::lock_guard lock(mutex_);
        if (auto it = coroutines_.find(handle); it != coroutines_.end()) {
            ++it->second.suspend_count;
            LOG_TRACE(logger_, "Coroutine suspended: {} (count: {})", 
                      it->second.name, it->second.suspend_count);
        }
#endif
    }
    
    /// 注册协程恢复
    void on_resume(void* handle) {
#ifdef CORONA_CORO_DEBUG
        std::lock_guard lock(mutex_);
        if (auto it = coroutines_.find(handle); it != coroutines_.end()) {
            ++it->second.resume_count;
            it->second.last_resumed = std::chrono::steady_clock::now();
            LOG_TRACE(logger_, "Coroutine resumed: {} (count: {})", 
                      it->second.name, it->second.resume_count);
        }
#endif
    }
    
    /// 打印所有活跃协程
    void dump_active_coroutines() {
#ifdef CORONA_CORO_DEBUG
        std::lock_guard lock(mutex_);
        LOG_INFO(logger_, "=== Active Coroutines ({}) ===", coroutines_.size());
        for (const auto& [handle, info] : coroutines_) {
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - info.created_at);
            LOG_INFO(logger_, "  {} @ {}: age={}ms, suspends={}, resumes={}",
                     info.name, handle, age.count(), 
                     info.suspend_count, info.resume_count);
        }
#endif
    }
    
    /// 获取统计信息
    struct Stats {
        std::size_t active_count;
        std::size_t total_created;
        std::size_t total_destroyed;
    };
    
    [[nodiscard]] Stats get_stats() const {
        return {
            .active_count = coroutines_.size(),
            .total_created = total_created_.load(),
            .total_destroyed = total_destroyed_.load()
        };
    }
    
private:
    CoroutineDebugger() {
#ifdef CORONA_CORO_DEBUG
        logger_ = quill::get_logger("CoroDebug");
#endif
    }
    
    std::mutex mutex_;
    std::unordered_map<void*, CoroutineInfo> coroutines_;
    std::atomic<std::size_t> total_created_{0};
    std::atomic<std::size_t> total_destroyed_{0};
#ifdef CORONA_CORO_DEBUG
    quill::Logger* logger_ = nullptr;
#endif
};

/// 调试宏（仅在 CORONA_CORO_DEBUG 启用时生效）
#ifdef CORONA_CORO_DEBUG
    #define CORO_DEBUG_CREATE(h, name) \
        Corona::Kernel::Coro::CoroutineDebugger::instance().on_create(h, name)
    #define CORO_DEBUG_DESTROY(h) \
        Corona::Kernel::Coro::CoroutineDebugger::instance().on_destroy(h)
    #define CORO_DEBUG_SUSPEND(h) \
        Corona::Kernel::Coro::CoroutineDebugger::instance().on_suspend(h)
    #define CORO_DEBUG_RESUME(h) \
        Corona::Kernel::Coro::CoroutineDebugger::instance().on_resume(h)
#else
    #define CORO_DEBUG_CREATE(h, name) ((void)0)
    #define CORO_DEBUG_DESTROY(h) ((void)0)
    #define CORO_DEBUG_SUSPEND(h) ((void)0)
    #define CORO_DEBUG_RESUME(h) ((void)0)
#endif

} // namespace Corona::Kernel::Coro
```

---

## 12. 后续扩展

### 12.1 计划中的功能

- [ ] `AsyncMutex` - 异步互斥锁
- [ ] `AsyncChannel<T>` - 异步通道（类似 Go channel）
- [ ] `AsyncSemaphore` - 异步信号量
- [ ] `Timeout<T>` - 带超时的等待
- [ ] `Retry<T>` - 自动重试机制
- [ ] `Throttle` - 节流控制
- [ ] `AsyncScope` - 结构化并发作用域

### 12.2 潜在集成点

协程模块保持独立，但可以被其他模块使用：

```cpp
// 其他模块可以这样使用协程
#include <corona/kernel/coro/coro.h>

// 示例：在 VFS 模块中包装异步文件操作
namespace Corona::Kernel::VFS {
    Coro::Task<std::vector<char>> async_read_file(const std::string& path);
}

// 示例：在网络模块中包装异步 I/O
namespace Corona::Kernel::Net {
    Coro::Task<Response> async_http_get(const std::string& url);
}
```

---

## 参考资料

- [C++20 Coroutines](https://en.cppreference.com/w/cpp/language/coroutines)
- [Lewis Baker's Coroutine Blog Series](https://lewissbaker.github.io/)
- [cppcoro Library](https://github.com/lewissbaker/cppcoro)
- [libunifex](https://github.com/facebookexperimental/libunifex)
