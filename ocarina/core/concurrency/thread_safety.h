//
// Created by Zero on 2023/6/22.
//

#pragma once

#include <mutex>

namespace ocarina {
template<typename Mutex = std::mutex>
class thread_safety {
public:
    using mutex_type = Mutex;

private:
    mutable Mutex mutex_{};

public:
    thread_safety() = default;
    template<typename F>
    decltype(auto) with_lock(F &&f) const noexcept {
        std::lock_guard lock{mutex_};
        return std::invoke(std::forward<F>(f));
    }
};

template<>
class thread_safety<void> {
public:
    template<typename F>
    decltype(auto) with_lock(F &&f) const noexcept {
        return std::invoke(std::forward<F>(f));
    }
};

template<bool thread_safe, typename Mutex = std::mutex>
using conditional_mutex = std::conditional<thread_safe, Mutex, void>;

template<bool thread_safe, typename Mutex = std::mutex>
using conditional_mutex_t = typename conditional_mutex<thread_safe, Mutex>::type;

}// namespace ocarina