#pragma once

#include <coroutine>
#include <exception>
#include <iterator>
#include <optional>
#include <type_traits>
#include <utility>

namespace Corona::Kernel::Coro {

// 前向声明
template <typename T>
class Generator;

namespace detail {

/**
 * @brief Generator Promise 类型
 *
 * @tparam T 生成的值类型
 */
template <typename T>
class GeneratorPromise {
   public:
    using value_type = std::remove_reference_t<T>;
    using reference_type = std::conditional_t<std::is_reference_v<T>, T, T&>;
    using pointer_type = value_type*;

    GeneratorPromise() = default;

    Generator<T> get_return_object() noexcept;

    /**
     * @brief 惰性启动：创建后不立即执行
     */
    std::suspend_always initial_suspend() noexcept { return {}; }

    /**
     * @brief 完成后挂起，让调用者有机会检查完成状态
     */
    std::suspend_always final_suspend() noexcept { return {}; }

    /**
     * @brief yield 值
     *
     * 保存当前值并挂起协程
     *
     * @param value 要生成的值
     */
    std::suspend_always yield_value(value_type& value) noexcept {
        current_value_ = std::addressof(value);
        return {};
    }

    /**
     * @brief yield 右值
     */
    std::suspend_always yield_value(value_type&& value) noexcept {
        current_value_ = std::addressof(value);
        return {};
    }

    /**
     * @brief 生成器不应该 return 值
     */
    void return_void() noexcept {}

    /**
     * @brief 处理未捕获异常
     */
    void unhandled_exception() noexcept { exception_ = std::current_exception(); }

    /**
     * @brief 获取当前值
     */
    reference_type value() const noexcept { return static_cast<reference_type>(*current_value_); }

    /**
     * @brief 重新抛出保存的异常
     */
    void rethrow_if_exception() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

    /**
     * @brief 禁止在生成器中 co_await
     */
    template <typename U>
    std::suspend_never await_transform(U&&) = delete;

   private:
    pointer_type current_value_ = nullptr;
    std::exception_ptr exception_;
};

}  // namespace detail

/**
 * @brief 生成器类型
 *
 * Generator<T> 用于惰性生成值序列，支持 co_yield 语法。
 * 生成器是单次遍历的（single-pass），不可拷贝。
 *
 * 特性：
 * - 惰性求值：只有在迭代时才计算值
 * - 支持 range-based for 循环
 * - 支持手动迭代（next() 方法）
 * - 异常传播：正确捕获和重新抛出协程内的异常
 *
 * 使用示例：
 * @code
 * Generator<int> range(int start, int end) {
 *     for (int i = start; i < end; ++i) {
 *         co_yield i;
 *     }
 * }
 *
 * // 使用 range-based for
 * for (int n : range(0, 10)) {
 *     std::cout << n << " ";
 * }
 *
 * // 使用 next() 方法
 * auto gen = range(0, 5);
 * while (auto val = gen.next()) {
 *     std::cout << *val << " ";
 * }
 * @endcode
 *
 * @tparam T 生成的值类型
 */
template <typename T>
class Generator {
   public:
    using promise_type = detail::GeneratorPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    using value_type = typename promise_type::value_type;
    using reference_type = typename promise_type::reference_type;

    /**
     * @brief 迭代器类型
     *
     * 支持 range-based for 循环
     */
    class Iterator {
       public:
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = Generator::value_type;
        using reference = Generator::reference_type;
        using pointer = value_type*;

        Iterator() noexcept = default;

        explicit Iterator(handle_type handle) noexcept : handle_(handle) {}

        /**
         * @brief 前置递增：生成下一个值
         */
        Iterator& operator++() {
            handle_.resume();
            if (handle_.done()) {
                handle_.promise().rethrow_if_exception();
                handle_ = nullptr;
            }
            return *this;
        }

        /**
         * @brief 后置递增
         */
        void operator++(int) { ++*this; }

        /**
         * @brief 解引用：获取当前值
         */
        [[nodiscard]] reference operator*() const noexcept { return handle_.promise().value(); }

        /**
         * @brief 成员访问
         */
        [[nodiscard]] pointer operator->() const noexcept {
            return std::addressof(handle_.promise().value());
        }

        /**
         * @brief 相等比较
         */
        [[nodiscard]] bool operator==(const Iterator& other) const noexcept {
            return handle_ == other.handle_;
        }

        /**
         * @brief 不等比较
         */
        [[nodiscard]] bool operator!=(const Iterator& other) const noexcept {
            return !(*this == other);
        }

        /**
         * @brief 与 sentinel 比较（用于 end()）
         */
        [[nodiscard]] bool operator==(std::default_sentinel_t) const noexcept {
            return handle_ == nullptr || handle_.done();
        }

       private:
        handle_type handle_;
    };

    // ========================================
    // 构造与析构
    // ========================================

    Generator() noexcept = default;

    explicit Generator(handle_type h) noexcept : handle_(h) {}

    ~Generator() {
        if (handle_) {
            handle_.destroy();
        }
    }

    // 禁止拷贝
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    // 支持移动
    Generator(Generator&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    Generator& operator=(Generator&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    // ========================================
    // 范围接口
    // ========================================

    /**
     * @brief 获取起始迭代器
     *
     * 首次调用会启动生成器执行到第一个 co_yield
     *
     * @return 起始迭代器
     */
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

    /**
     * @brief 获取结束迭代器（sentinel）
     *
     * @return 空迭代器
     */
    [[nodiscard]] Iterator end() noexcept { return Iterator{}; }

    /**
     * @brief 获取结束标记（C++20 sentinel）
     */
    [[nodiscard]] std::default_sentinel_t end_sentinel() const noexcept {
        return std::default_sentinel;
    }

    // ========================================
    // 手动迭代接口
    // ========================================

    /**
     * @brief 获取下一个值
     *
     * @return 下一个值，如果生成器已完成则返回 nullopt
     * @throws 生成器内部抛出的异常
     */
    [[nodiscard]] std::optional<value_type> next() {
        if (!handle_ || handle_.done()) {
            return std::nullopt;
        }
        handle_.resume();
        if (handle_.done()) {
            handle_.promise().rethrow_if_exception();
            return std::nullopt;
        }
        return handle_.promise().value();
    }

    // ========================================
    // 状态查询
    // ========================================

    /**
     * @brief 检查生成器是否有效
     */
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }

    /**
     * @brief 检查生成器是否已完成
     */
    [[nodiscard]] bool done() const noexcept { return !handle_ || handle_.done(); }

   private:
    handle_type handle_;
};

// ========================================
// Promise 实现
// ========================================

namespace detail {

template <typename T>
Generator<T> GeneratorPromise<T>::get_return_object() noexcept {
    return Generator<T>{std::coroutine_handle<GeneratorPromise<T>>::from_promise(*this)};
}

}  // namespace detail

}  // namespace Corona::Kernel::Coro
