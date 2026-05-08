//
// Created by Zero on 14/09/2022.
//

#pragma once

#include "basic_types.h"
#include "interval.h"
#include "math/base.h"
#include "dsl/core/var.h"
#include "core/type_system/type_desc.h"
#include "dsl/types/struct.h"
#include "dsl/api/builtin.h"

namespace ocarina::inline math {
template<typename T, size_t N>
struct TBox {
public:
    using scalar_t = T;
    using vector_t = Vector<T, N>;
    vector_t lower, upper;

public:
    TBox() : lower(pos_infinity_v<scalar_t>),
             upper(neg_infinity_v<scalar_t>) {}

    explicit inline TBox(const vector_t &v)
        : lower(v), upper(v) {}

    TBox(const vector_t &lo, const vector_t &hi)
        : lower(lo), upper(hi) {}

    [[nodiscard]] TBox including(const vector_t &other) const {
        return TBox(min(lower, other), max(upper, other));
    }

    [[nodiscard]] TBox including(const TBox &other) const {
        return TBox(min(lower, other.lower), max(upper, other.upper));
    }

    TBox &extend(const vector_t &other) {
        lower = min(lower, other);
        upper = max(upper, other);
        return *this;
    }

    TBox &extend(const TBox &other) {
        lower = min(lower, other.lower);
        upper = max(upper, other.upper);
        return *this;
    }

    [[nodiscard]] interval<scalar_t> get_slab(const uint32_t dim) noexcept {
        return interval<scalar_t>(lower[dim], upper[dim]);
    }

    [[nodiscard]] vector_t offset(vector_t p) const noexcept {
        return (p - lower) / span();
    }

    [[nodiscard]] bool contains(const vector_t &point) const noexcept {
        return all(point >= lower) && all(upper >= point);
    }

    [[nodiscard]] bool contains(const TBox &other) const noexcept {
        return all(other.lower >= lower) && all(upper >= other.upper);
    }

    [[nodiscard]] bool overlap(const TBox &other) const noexcept {
        return contains(other.lower) || contains(other.upper);
    }

    [[nodiscard]] scalar_t radius() const noexcept {
        return length(upper - lower) * 0.5f;
    }

    [[nodiscard]] vector_t lerp(vector_t t) const noexcept {
        return ocarina::lerp(t, lower, upper);
    }

    [[nodiscard]] vector_t center() const noexcept {
        return (lower + upper) * 0.5f;
    }

    [[nodiscard]] vector_t span() const noexcept {
        return upper - lower;
    }

    [[nodiscard]] vector_t size() const noexcept {
        return upper - lower;
    }

    [[nodiscard]] scalar_t volume() const noexcept {
        return ocarina::volume(upper - lower);
    }

    [[nodiscard]] scalar_t area() const noexcept {
        static_assert(N == 2 || N == 3);
        vector_t diag = upper - lower;
        if constexpr (N == 2) {
            return diag.x * diag.y;
        } else if constexpr (N == 3) {
            return 2 * (diag.x * diag.y + diag.x * diag.z + diag.y * diag.z);
        }
    }

    [[nodiscard]] bool empty() const noexcept {
        return any(upper < lower);
    }

    [[nodiscard]] auto advance(Vector<scalar_t, 2> p) const noexcept {
        ++p.x;
        if (p.x == upper.x) {
            p.x = lower.x;
            ++p.y;
        }
        return p;
    }

    template<typename Func>
    void for_each(Func func) const noexcept {
        static_assert(std::is_integral_v<scalar_t> || std::is_unsigned_v<scalar_t>,
                      "scalar_t must be unsigned or integral!");
        auto p = lower;
        do {
            func(p);
            p = advance(p);
        } while (all(p < upper));
    }
};

#define OC_DEFINE_BOX(scalar_t, suffix)   \
    using Box##2##suffix = TBox<scalar_t, 2>; \
    using Box##3##suffix = TBox<scalar_t, 3>; \
    using Box##4##suffix = TBox<scalar_t, 4>;

OC_DEFINE_BOX(int32_t, i);
OC_DEFINE_BOX(uint32_t, u);
OC_DEFINE_BOX(float, f);
OC_DEFINE_BOX(double, d);
OC_DEFINE_BOX(int64_t, l);

#undef OC_DEFINE_BOX

template<EPort p = D>
[[nodiscard]] oc_bool<p> in_screen(const oc_int2<p> &pixel,
                                   const oc_int2<p> &res) noexcept {
    return all(pixel >= 0) && all(pixel < res);
}

}

// namespace ocarina::math

// clang-format off
OC_STRUCT(ocarina, Box2u, lower, upper) {
    [[nodiscard]] Bool contains(const Uint2 &point) const noexcept {
        return ocarina::all(point >= lower) && ocarina::all(upper >= point);
    }
};
// clang-format on