#pragma once

#include <chrono>
#include <concepts>
#include <coroutine>
#include <functional>
#include <type_traits>

namespace Corona::Kernel::Coro {

/**
 * @brief Awaitable 概念：可被 co_await 的类型
 *
 * 任何实现了 await_ready/await_suspend/await_resume 三件套的类型
 * 都可以被 co_await 表达式使用。
 *
 * @tparam T 待检查的类型
 */
template <typename T>
concept Awaitable = requires(T t) {
    { t.await_ready() } -> std::convertible_to<bool>;
    { t.await_suspend(std::coroutine_handle<>{}) };
    { t.await_resume() };
};

/**
 * @brief Awaiter 概念：带返回值类型约束的等待器
 *
 * 比 Awaitable 更严格，额外约束 await_resume() 的返回类型。
 *
 * @tparam T 待检查的类型
 * @tparam Result await_resume() 的期望返回类型
 */
template <typename T, typename Result = void>
concept Awaiter = Awaitable<T> && requires(T t) {
    { t.await_resume() } -> std::convertible_to<Result>;
};

/**
 * @brief VoidAwaiter 概念：无返回值的等待器
 *
 * @tparam T 待检查的类型
 */
template <typename T>
concept VoidAwaiter = Awaitable<T> && requires(T t) {
    { t.await_resume() } -> std::same_as<void>;
};

/**
 * @brief Promise 概念：协程承诺类型约束
 *
 * 定义协程的 promise_type 必须满足的接口要求：
 * - get_return_object(): 创建协程返回对象
 * - initial_suspend(): 协程开始时的挂起行为
 * - final_suspend(): 协程结束时的挂起行为（必须 noexcept）
 * - unhandled_exception(): 处理未捕获的异常
 *
 * @tparam P 待检查的 Promise 类型
 */
template <typename P>
concept PromiseType = requires(P p) {
    { p.get_return_object() };
    { p.initial_suspend() } -> Awaitable;
    { p.final_suspend() } noexcept -> Awaitable;
    { p.unhandled_exception() };
};

/**
 * @brief VoidPromise 概念：返回 void 的协程 Promise
 *
 * @tparam P 待检查的 Promise 类型
 */
template <typename P>
concept VoidPromise = PromiseType<P> && requires(P p) {
    { p.return_void() };
};

/**
 * @brief ValuePromise 概念：返回值的协程 Promise
 *
 * @tparam P 待检查的 Promise 类型
 * @tparam T 返回值类型
 */
template <typename P, typename T>
concept ValuePromise = PromiseType<P> && requires(P p, T value) {
    { p.return_value(std::move(value)) };
};

/**
 * @brief Executor 概念：执行器类型约束
 *
 * 执行器负责调度和执行任务，必须提供：
 * - execute(): 立即执行任务
 * - execute_after(): 延迟执行任务
 *
 * @tparam E 待检查的执行器类型
 */
template <typename E>
concept Executor = requires(E e, std::function<void()> f) {
    { e.execute(std::move(f)) };
    { e.execute_after(std::move(f), std::chrono::milliseconds{}) };
};

/**
 * @brief Invocable 概念的便捷别名
 *
 * 检查类型是否可以无参调用并返回 bool
 */
template <typename F>
concept BoolPredicate = std::invocable<F> &&
                        std::convertible_to<std::invoke_result_t<F>, bool>;

/**
 * @brief 检查类型是否为有效的协程返回类型
 *
 * 有效的协程返回类型必须包含嵌套的 promise_type
 */
template <typename T>
concept CoroutineReturnType = requires {
    typename T::promise_type;
};

}  // namespace Corona::Kernel::Coro
