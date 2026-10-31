//
// Created by Zero on 15/09/2022.
//

#pragma once

#include "basic_types.h"
#include "core/concepts.h"
#include "constants.h"

namespace ocarina {

inline namespace math {
template<typename T>
struct interval {
    using scalar_t = T;

    T begin;
    T end;

    inline interval()
        : begin(neg_infinity_v<T>),
          end(pos_infinity_v<T>) {}

    inline interval(T begin, T end) : begin(begin), end(end) {}

    [[nodiscard]] bool contains(const T &t) const { return t >= begin && t <= end; }

    [[nodiscard]] bool is_empty() const { return begin > end; }

    [[nodiscard]] T center() const { return (begin + end) / 2; }

    [[nodiscard]] T span() const { return end - begin; }

    [[nodiscard]] T diagonal() const { return end - begin; }

    [[nodiscard]] interval<T> &extend(const T &t) {
        begin = min(begin, t);
        end = max(end, t);
        return *this;
    }

    [[nodiscard]] interval<T> &extend(const interval<T> &t) {
        begin = min(begin, t.begin);
        end = max(end, t.end);
        return *this;
    }

    [[nodiscard]] interval<T> including(const T &t) { return interval<T>(std::min(begin, t), std::max(end, t)); }

    [[nodiscard]] static interval<T> positive() {
        return interval<T>(0.f, empty_range_upper<T>());
    }
};

template<typename T>
[[nodiscard]] interval<T> build_interval(const T &a, const T &b) { return interval<T>(std::min(a, b), std::max(a, b)); }

template<typename T>
[[nodiscard]] interval<T> intersect(const interval<T> &a, const interval<T> &b) {
    return interval<T>(max(a.begin, b.begin), min(a.end, b.end));
}

}
}// namespace ocarina::math

template<typename T>
[[nodiscard]] ocarina::interval<T> operator*(const ocarina::interval<T> &a, const T &b) {
    return ocarina::build_interval<T>(a.begin * b, a.end * b);
}

template<typename T>
[[nodiscard]] ocarina::interval<T> operator-(const ocarina::interval<T> &a, const T &b) {
    return interval<T>(a.begin - b, a.end - b);
}

template<typename T>
[[nodiscard]] bool operator==(const ocarina::interval<T> &a, const ocarina::interval<T> &b) {
    return a.begin == b.begin && a.end == b.end;
}

template<typename T>
[[nodiscard]] bool operator!=(const ocarina::interval<T> &a,
                              const ocarina::interval<T> &b) {
    return !(a == b);
}