//
// Created by Zero on 2023/7/10.
//

#include "thread_pool.h"
#include "core/util/logging.h"

#if (!defined(__clang_major__) || __clang_major__ >= 14) && defined(__cpp_lib_barrier)
#define OC_USE_STD_BARRIER
#endif

#ifdef OC_USE_STD_BARRIER
#include <barrier>
#endif

namespace ocarina {

namespace detail {

[[nodiscard]] static auto &is_worker_thread() noexcept {
    static thread_local auto is_worker = false;
    return is_worker;
}

[[nodiscard]] static auto &worker_thread_index() noexcept {
    static thread_local auto id = 0u;
    return id;
}

static inline void check_not_in_worker_thread(std::string_view f) noexcept {
    if (is_worker_thread()) [[unlikely]] {
        std::ostringstream oss;
        oss << std::this_thread::get_id();
        OC_ERROR_FORMAT(
            "Invoking ThreadPool::{}() "
            "from worker thread {}.",
            f, oss.str());
    }
}

}// namespace detail

#ifdef OC_USE_STD_BARRIER
struct Barrier : std::barrier<> {
    using std::barrier<>::barrier;
};
#else
// reference: https://github.com/yohhoy/yamc/blob/master/include/yamc_barrier.hpp
class Barrier {
private:
    uint n_;
    uint counter_;
    uint phase_;
    std::condition_variable cv_;
    std::mutex mutex_;

public:
    explicit Barrier(uint n) noexcept
        : n_{n}, counter_{n}, phase_{0u} {}
    void arrive_and_wait() noexcept {
        std::unique_lock lock{mutex_};
        auto arrive_phase = phase_;
        if (--counter_ == 0u) {
            counter_ = n_;
            phase_++;
            cv_.notify_all();
        }
        while (phase_ <= arrive_phase) {
            cv_.wait(lock);
        }
    }
};
#endif

ThreadPool::ThreadPool(size_t num_threads) noexcept : should_stop_{false} {
    if (num_threads == 0u) {
        num_threads = std::max(
            std::thread::hardware_concurrency(), 1u);
    }
    dispatch_barrier_ = make_unique<Barrier>(num_threads);
    synchronize_barrier_ = make_unique<Barrier>(num_threads + 1u /* main thread */);
    threads_.reserve(num_threads);
    for (auto i = 0u; i < num_threads; i++) {
        threads_.emplace_back(std::thread{[this, i] {
            detail::is_worker_thread() = true;
            detail::worker_thread_index() = i;
            for (;;) {
                std::unique_lock lock{mutex_};
                cv_.wait(lock, [this] { return !tasks_.empty() || should_stop_; });
                if (should_stop_ && tasks_.empty()) [[unlikely]] { break; }
                auto task = std::move(tasks_.front());
                tasks_.pop();
                lock.unlock();
                task();
            }
        }});
    }
    OC_INFO_FORMAT("Created thread pool with {} thread{}.",
                   num_threads, num_threads == 1u ? "" : "s");
}

void ThreadPool::barrier() noexcept {
    detail::check_not_in_worker_thread("barrier");
    _dispatch_all([this] { dispatch_barrier_->arrive_and_wait(); });
}

void ThreadPool::synchronize() noexcept {
    detail::check_not_in_worker_thread("synchronize");
    while (task_count() != 0u) {
        _dispatch_all([this] { synchronize_barrier_->arrive_and_wait(); });
        synchronize_barrier_->arrive_and_wait();
    }
}

void ThreadPool::_dispatch(std::function<void()> task) noexcept {
    {
        std::scoped_lock lock{mutex_};
        tasks_.emplace(std::move(task));
    }
    cv_.notify_one();
}

void ThreadPool::_dispatch_all(std::function<void()> task, size_t max_threads) noexcept {
    {
        std::scoped_lock lock{mutex_};
        for (auto i = 0u; i < std::min(threads_.size(), max_threads) - 1u; i++) {
            tasks_.emplace(task);
        }
        tasks_.emplace(std::move(task));
    }
    cv_.notify_all();
}

ThreadPool::~ThreadPool() noexcept {
    {
        std::scoped_lock lock{mutex_};
        should_stop_ = true;
    }
    cv_.notify_all();
    for (auto &&t : threads_) { t.join(); }
}

ThreadPool &ThreadPool::instance() noexcept {
    static ThreadPool pool;
    return pool;
}

uint ThreadPool::task_count() const noexcept {
    return task_count_.load();
}

uint ThreadPool::worker_thread_index() noexcept {
    OC_ASSERT(detail::is_worker_thread());
    return detail::worker_thread_index();
}

}// namespace ocarina