//
// Created by Zero on 15/05/2022.
//

#pragma once

#include "math/basic_traits.h"
#include "core/concepts.h"
#include "math/base.h"
#include "core/util/logging.h"

namespace ocarina {

template<typename T, typename U>
requires is_integral_v<T> && is_integral_v<U>
OC_NODISCARD static constexpr auto
mem_offset(T offset, U alignment) noexcept {
    return (offset + alignment - 1u) / alignment * alignment;
}

inline size_t structure_size(ocarina::span<const MemoryBlock> members) noexcept {
    size_t size = 0;
    size_t alignment = 0;
    for (const MemoryBlock block : members) {
        size = mem_offset(size, block.alignment);
        size += block.size;
        alignment = std::max({alignment, block.max_member_size, block.alignment});
    }
    auto mod = size % alignment;
    if (mod != 0) {
        size += (alignment)-mod;
    }
    return size;
}

inline size_t structure_alignment(span<const MemoryBlock> members) noexcept {
    size_t ret = 0;
    for (const MemoryBlock block : members) {
        ret = max(block.alignment, ret);
    }
    return ret;
}

template<typename T, typename V>
requires concepts::iterable<V> && concepts::iterable<T>
void append(T &v1, V &&v2) {
    v1.insert(v1.end(), OC_FORWARD(v2).begin(), OC_FORWARD(v2).end());
}

template<typename T, typename F>
requires concepts::subscriptable<T>
[[nodiscard]] uint get_index(T &t, F &&func) noexcept {
    for (uint i = 0; i < t.size(); ++i) {
        if (func(t.at(i))) {
            return i;
        }
    }
    return InvalidUI32;
}

inline namespace size_literals {
[[nodiscard]] constexpr auto operator""_kb(size_t bytes) noexcept {
    return static_cast<size_t>(bytes * 1024u);
}

[[nodiscard]] constexpr auto operator""_mb(size_t bytes) noexcept {
    return static_cast<size_t>(bytes * sqr(1024u));
}

[[nodiscard]] constexpr auto operator""_gb(size_t bytes) noexcept {
    return static_cast<size_t>(bytes * Pow<3>(1024u));
}
}// namespace size_literals

[[nodiscard]] constexpr float to_kb(size_t bytes) noexcept {
    return static_cast<float>(bytes) / 1024;
}

[[nodiscard]] constexpr float to_mb(size_t bytes) noexcept {
    return static_cast<float>(bytes) / sqr(1024);
}

[[nodiscard]] constexpr float to_gb(size_t bytes) noexcept {
    return static_cast<double>(bytes) / Pow<3>(1024u);
}

[[nodiscard]] inline string bytes_string(size_t bytes) noexcept {
    if (bytes > 1_gb) {
        return ocarina::format("{:.2f} gb", to_gb(bytes));
    } else if (bytes > 1_mb) {
        return ocarina::format("{:.2f} mb", to_mb(bytes));
    } else if (bytes > 1_kb) {
        return ocarina::format("{:.2f} kb", to_kb(bytes));
    } else {
        return ocarina::format("{} byte", bytes);
    }
}

template<class T>
inline void hash_combine(std::size_t &s, const T &v) {
    std::hash<T> h;
    s ^= h(v) + 0x9e3779b9 + (s << 6) + (s >> 2);
}

class Guarded {
public:
    virtual void begin() noexcept {}
    virtual void end() noexcept {}
};

class Clock : public Guarded {
public:
    using SystemClock = std::chrono::high_resolution_clock;
    using Tick = std::chrono::high_resolution_clock::time_point;

private:
    Tick _last;
    ocarina::string _tag;

public:
    explicit Clock(const string &tag) noexcept
        : _last{SystemClock::now()}, _tag(tag) {}
    Clock() noexcept
        : _last{SystemClock::now()}, _tag("") {}
    void start() noexcept { _last = SystemClock::now(); }
    [[nodiscard]] auto elapse_ms() const noexcept {
        auto curr = SystemClock::now();
        using namespace std::chrono_literals;
        return (curr - _last) / 1ns * 1e-6;
    }
    [[nodiscard]] auto elapse_s() const noexcept {
        return elapse_ms() / 1000;
    }

    void begin() noexcept override {
        start();
        if (_tag.empty()) { return; }
        OC_INFO_FORMAT("task {} start !", _tag.c_str());
    }

    void end() noexcept override {
        if (_tag.empty()) { return; }
        if (elapse_ms() < 1000) {
            OC_INFO_FORMAT("task {} is take {:.2f} ms", _tag.c_str(), elapse_ms());
        } else {
            OC_INFO_FORMAT("task {} is take {:.2f} s", _tag.c_str(), elapse_s());
        }
    }
};

template<typename T>
class Guard {
private:
    T t;

public:
    explicit Guard(T t) : t(t) {
        t.begin();
    }

    ~Guard() {
        t.end();
    }
};

#define TIMER(task_name) ocarina::Guard<Clock> __##task_name(Clock(#task_name));
#define TIMER_TAG(task_name, tag) ocarina::Guard<Clock> __##task_name(Clock(tag));

}// namespace ocarina