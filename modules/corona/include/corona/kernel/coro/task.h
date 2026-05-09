#pragma once

#include <atomic>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Corona::Kernel::Coro {

/**
 * @brief 任务状态枚举
 */
enum class TaskState : uint8_t {
    Pending,    ///< 等待执行
    Running,    ///< 正在执行
    Completed,  ///< 执行完成
    Failed      ///< 执行失败
};

// 前向声明
template <typename T>
class Task;

namespace detail {

/**
 * @brief 同步等待事件
 */
struct SyncWaitEvent {
    std::atomic<bool> set{false};
    void wait() {
        while (!set.load(std::memory_order_acquire)) {
            set.wait(false);
        }
    }
    void signal() {
        set.store(true, std::memory_order_release);
        set.notify_one();
    }
};

struct SyncWaitTask;

/**
 * @brief 同步等待任务 Promise
 */
struct SyncWaitTaskPromise {
    SyncWaitEvent* event = nullptr;
    std::exception_ptr exception;

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<SyncWaitTaskPromise> h) noexcept {
            if (h.promise().event) h.promise().event->signal();
        }
        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { exception = std::current_exception(); }

    SyncWaitTask get_return_object() noexcept;
};

/**
 * @brief 同步等待任务
 */
struct SyncWaitTask {
    using promise_type = SyncWaitTaskPromise;
    std::coroutine_handle<promise_type> handle;

    explicit SyncWaitTask(std::coroutine_handle<promise_type> h) : handle(h) {}
    ~SyncWaitTask() {
        if (handle) handle.destroy();
    }
    SyncWaitTask(SyncWaitTask&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    SyncWaitTask& operator=(SyncWaitTask&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }

    void start(SyncWaitEvent* event) {
        handle.promise().event = event;
        handle.resume();
    }
};

inline SyncWaitTask SyncWaitTaskPromise::get_return_object() noexcept {
    return SyncWaitTask{std::coroutine_handle<SyncWaitTaskPromise>::from_promise(*this)};
}

/**
 * @brief Task Promise 基类
 *
 * 包含所有 Task Promise 类型共享的功能
 */
class TaskPromiseBase {
   public:
    /**
     * @brief 协程开始时的行为：惰性启动（lazy）
     *
     * 返回 suspend_always 表示协程创建后不会立即执行，
     * 而是等待调用者通过 co_await 或 resume() 启动。
     */
    std::suspend_always initial_suspend() noexcept { return {}; }

    /**
     * @brief 处理未捕获异常
     *
     * 将异常保存起来，在 await_resume 时重新抛出
     */
    void unhandled_exception() noexcept { exception_ = std::current_exception(); }

    /**
     * @brief 设置等待此任务完成的协程句柄
     *
     * @param cont 等待者的协程句柄
     */
    void set_continuation(std::coroutine_handle<> cont) noexcept { continuation_ = cont; }

    /**
     * @brief 检查是否有异常
     */
    [[nodiscard]] bool has_exception() const noexcept { return exception_ != nullptr; }

    /**
     * @brief 重新抛出保存的异常
     */
    void rethrow_if_exception() const {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

   protected:
    /**
     * @brief Final Awaiter
     *
     * 协程结束时使用对称转移，直接跳转到等待者，避免栈溢出
     */
    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
            auto& promise = h.promise();
            if (promise.continuation_) {
                return promise.continuation_;
            }
            return std::noop_coroutine();
        }

        void await_resume() noexcept {}
    };

    std::exception_ptr exception_;
    std::coroutine_handle<> continuation_;
};

/**
 * @brief 带返回值的 Task Promise
 *
 * @tparam T 返回值类型
 */
template <typename T>
class TaskPromise : public TaskPromiseBase {
   public:
    Task<T> get_return_object() noexcept;

    /**
     * @brief 协程结束时的行为
     */
    FinalAwaiter final_suspend() noexcept { return {}; }

    /**
     * @brief 设置返回值
     *
     * @tparam U 值类型（必须可转换为 T）
     * @param value 返回值
     */
    template <typename U>
        requires std::convertible_to<U, T>
    void return_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, U>) {
        result_.emplace(std::forward<U>(value));
    }

    /**
     * @brief 获取结果（左值引用版本）
     *
     * @return 结果的左值引用
     * @throws 保存的异常（如果有）
     */
    T& result() & {
        rethrow_if_exception();
        return *result_;
    }

    /**
     * @brief 获取结果（右值引用版本）
     *
     * @return 结果的右值引用
     * @throws 保存的异常（如果有）
     */
    T&& result() && {
        rethrow_if_exception();
        return std::move(*result_);
    }

   private:
    std::optional<T> result_;
};

/**
 * @brief void 特化的 Task Promise
 */
template <>
class TaskPromise<void> : public TaskPromiseBase {
   public:
    Task<void> get_return_object() noexcept;

    FinalAwaiter final_suspend() noexcept { return {}; }

    void return_void() noexcept {}

    void result() const { rethrow_if_exception(); }
};

}  // namespace detail

}  // namespace Corona::Kernel::Coro

// 特化 coroutine_traits 以支持 SyncWaitTask
template <typename... Args>
struct std::coroutine_traits<Corona::Kernel::Coro::detail::SyncWaitTask, Args...> {
    using promise_type = Corona::Kernel::Coro::detail::SyncWaitTaskPromise;
};

namespace Corona::Kernel::Coro {

/**
 * @brief 异步任务类型
 *
 * Task<T> 是最常用的协程返回类型，表示一个最终会产生 T 类型值的异步操作。
 *
 * 特性：
 * - 惰性执行：创建后不会立即执行，需要 co_await 或调用 get()/resume()
 * - 对称转移：避免深层协程嵌套导致的栈溢出
 * - 异常传播：正确捕获和重新抛出协程内的异常
 * - 移动语义：支持移动，禁止拷贝
 *
 * 使用示例：
 * @code
 * Task<int> async_add(int a, int b) {
 *     co_return a + b;
 * }
 *
 * Task<int> compute() {
 *     int x = co_await async_add(1, 2);
 *     int y = co_await async_add(3, 4);
 *     co_return x + y;
 * }
 *
 * // 同步等待结果
 * int result = compute().get();
 * @endcode
 *
 * @tparam T 任务的返回值类型，默认为 void
 */
template <typename T = void>
class Task {
   public:
    using promise_type = detail::TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    using value_type = T;

    // ========================================
    // 构造与析构
    // ========================================

    /**
     * @brief 默认构造函数
     */
    Task() noexcept = default;

    /**
     * @brief 从协程句柄构造
     *
     * @param h 协程句柄
     */
    explicit Task(handle_type h) noexcept : handle_(h) {}

    /**
     * @brief 析构函数
     *
     * 销毁协程帧，释放资源
     */
    ~Task() {
        if (handle_) {
            handle_.destroy();
        }
    }

    // 禁止拷贝
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    /**
     * @brief 移动构造函数
     */
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    /**
     * @brief 移动赋值运算符
     */
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

    /**
     * @brief 检查是否可以立即返回结果
     *
     * @return 如果协程已完成则返回 true
     */
    [[nodiscard]] bool await_ready() const noexcept { return handle_ && handle_.done(); }

    /**
     * @brief 挂起当前协程并启动此任务
     *
     * 使用对称转移优化，直接跳转到此任务执行
     *
     * @param awaiting 等待者的协程句柄
     * @return 此任务的协程句柄
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        handle_.promise().set_continuation(awaiting);
        return handle_;  // 对称转移到此任务
    }

    /**
     * @brief 获取任务结果
     *
     * @return 任务的返回值
     * @throws 任务执行过程中抛出的异常
     */
    T await_resume() {
        if constexpr (std::is_void_v<T>) {
            handle_.promise().result();
        } else {
            return std::move(handle_.promise()).result();
        }
    }

    // ========================================
    // 同步等待接口
    // ========================================

    /**
     * @brief 阻塞等待任务完成并获取结果
     *
     * 使用同步等待机制，支持异步任务的等待。
     *
     * @return 任务的返回值
     * @throws 任务执行过程中抛出的异常
     */
    T get() {
        if (!handle_) {
            throw std::runtime_error("Task is invalid");
        }
        if (handle_.done()) {
            if constexpr (std::is_void_v<T>) {
                handle_.promise().result();
                return;
            } else {
                return std::move(handle_.promise()).result();
            }
        }

        detail::SyncWaitEvent event;
        std::exception_ptr ex;
        std::optional<std::conditional_t<std::is_void_v<T>, int, T>> result_val;

        // 创建一个同步等待任务来包装当前任务
        auto waiter = [&](Task<T>&& t) -> detail::SyncWaitTask {
            try {
                if constexpr (std::is_void_v<T>) {
                    co_await t;
                } else {
                    result_val = co_await t;
                }
            } catch (...) {
                ex = std::current_exception();
            }
            // event 在 promise 的 final_suspend 中被 signal
        }(std::move(*this));

        // 启动等待任务
        waiter.start(&event);

        // 阻塞等待
        event.wait();

        // 检查异常
        if (ex) {
            std::rethrow_exception(ex);
        }

        // 返回结果
        if constexpr (!std::is_void_v<T>) {
            return std::move(*result_val);
        }
    }

    // ========================================
    // 状态查询
    // ========================================

    /**
     * @brief 检查任务是否有效
     *
     * @return 如果持有有效的协程句柄则返回 true
     */
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }

    /**
     * @brief 检查任务是否完成
     *
     * @return 如果协程已完成则返回 true
     */
    [[nodiscard]] bool done() const noexcept { return handle_ && handle_.done(); }

    /**
     * @brief 启动或恢复任务执行
     *
     * 如果任务尚未完成，恢复协程执行
     */
    void resume() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    /**
     * @brief 获取底层协程句柄
     *
     * @return 协程句柄
     */
    [[nodiscard]] handle_type handle() const noexcept { return handle_; }

   private:
    handle_type handle_;
};

// ========================================
// Promise 实现
// ========================================

namespace detail {

template <typename T>
Task<T> TaskPromise<T>::get_return_object() noexcept {
    return Task<T>{std::coroutine_handle<TaskPromise<T>>::from_promise(*this)};
}

inline Task<void> TaskPromise<void>::get_return_object() noexcept {
    return Task<void>{std::coroutine_handle<TaskPromise<void>>::from_promise(*this)};
}

}  // namespace detail

}  // namespace Corona::Kernel::Coro
