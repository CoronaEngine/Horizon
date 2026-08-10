//
// Created by Zero on 19/09/2022.
//

#pragma once

#include "math/basic_types.h"
#include "dsl/dsl.h"

namespace ocarina {

inline namespace geometry {

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] auto cos_theta_2(const T &v) noexcept { return ocarina::sqr(v.z); }

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] auto cos_theta(const T &v) noexcept { return v.z; }

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] scalar_t<T> abs_cos_theta(const T &v) noexcept { return ocarina::abs(v.z); }

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] scalar_t<T> sin_theta_2(const T &v) noexcept { return 1.0f - cos_theta_2(v); }

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] scalar_t<T> sin_theta(const T &v) noexcept {
    scalar_t<T> temp = sin_theta_2(v);
    return ocarina::select(temp <= 0.f, 0.f, ocarina::sqrt(temp));
}

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] scalar_t<T> tan_theta(const T &v) noexcept {
    scalar_t<T> sin_theta2 = 1 - cos_theta_2(v);
    return ocarina::select(sin_theta2 <= 0.f, 0.f, ocarina::sqrt(sin_theta2) / cos_theta(v));
}

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] scalar_t<T> tan_theta_2(const T &v) noexcept {
    scalar_t<T> cos_theta2 = cos_theta_2(v);
    scalar_t<T> sin_theta2 = 1.f - cos_theta2;
    return ocarina::select(sin_theta2 <= 0.f, 0.f, sin_theta2 / cos_theta2);
}

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] scalar_t<T> sin_phi(const T &v) noexcept {
    scalar_t<T> sinTheta = sin_theta(v);
    return ocarina::select(sinTheta == 0, 1.f, ocarina::clamp(v.y / sinTheta, -1.f, 1.f));
}

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] scalar_t<T> cos_phi(const T &v) noexcept {
    scalar_t<T> sinTheta = sin_theta(v);
    return ocarina::select(sinTheta == 0.f, 1.f, ocarina::clamp(v.x / sinTheta, -1.f, 1.f));
}

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] scalar_t<T> sin_phi_2(const T &v) {
    scalar_t<T> sinTheta2 = sin_theta_2(v);
    return ocarina::select(sinTheta2 == 0.f, 0.f, ocarina::clamp(ocarina::sqr(v.y) / sinTheta2, 0.f, 1.f));
}

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] scalar_t<T> cos_phi_2(const T &v) {
    scalar_t<T> sinTheta2 = sin_theta_2(v);
    return ocarina::select(sinTheta2 == 0.f, 1.f, ocarina::clamp(ocarina::sqr(v.x) / sinTheta2, 0.f, 1.f));
}

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] Float phi(const T &v) {
    Float p = atan2(v.y, v.x);
    return ocarina::select(p < 0, p + 2 * Pi, p);
}

template<EPort p = D>
[[nodiscard]] oc_float3<p> spherical_direction(oc_float<p> sin_theta, oc_float<p> cos_theta,
                                               oc_float<p> sin_phi, oc_float<p> cos_phi) {
    return make_float3(sin_theta * cos_phi, sin_theta * sin_phi, cos_theta);
}

template<EPort p = D>
[[nodiscard]] oc_float3<p> spherical_direction(oc_float<p> sin_theta, oc_float<p> cos_theta,
                                               oc_float<p> phi) {
    return make_float3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
}

template<EPort p = D>
[[nodiscard]] oc_float3<p> spherical_direction(oc_float<p> theta, oc_float<p> phi) {
    return spherical_direction(sin(theta), cos(theta), phi);
}
template<EPort p = D>
[[nodiscard]] oc_float3<p> spherical_direction(oc_float<p> sin_theta, oc_float<p> cos_theta, oc_float<p> phi,
                                               const oc_float3<p> &x, const oc_float3<p> &y,
                                               const oc_float3<p> &z) {
    return sin_theta * cos(phi) * x + sin_theta * sin(phi) * y + cos_theta * z;
}

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] auto spherical_theta(const T &v) {
    return safe_acos(v.z);
}

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] auto spherical_phi(const T &v) {
    auto p = atan2(v.y, v.x);
    return select((p < 0), (p + 2 * Pi), p);
}

template<EPort p = D>
[[nodiscard]] oc_bool<p> is_right_hand(const oc_float3<p> &x, const oc_float3<p> &y,
                                       const oc_float3<p> &z) noexcept {
    return dot(cross(x, y), z) > 0;
}

template<typename T, bool normalized = true>
requires is_vector3_expr_v<T>
struct Frame {
public:
    using vec_ty = T;
    vec_ty x, y, z;
    static constexpr EPort port = is_dsl_v<T> ? D : H;

public:
    [[nodiscard]] vec_ty nx() const noexcept {
        if constexpr (normalized) {
            return x;
        } else {
            return normalize(x);
        }
    }

    [[nodiscard]] vec_ty ny() const noexcept {
        if constexpr (normalized) {
            return y;
        } else {
            return normalize(y);
        }
    }

    [[nodiscard]] vec_ty nz() const noexcept {
        if constexpr (normalized) {
            return z;
        } else {
            return normalize(z);
        }
    }

public:
    Frame() = default;
    Frame(const T &x, const T &y, const T &z)
        : x(x), y(y), z(z) {}

    explicit Frame(const T &normal)
        : z(normal) {
        ocarina::coordinate_system(z, x, y);
    }

    void set(const vec_ty &x_, const vec_ty &y_, const vec_ty &z_) {
        x = x_;
        y = y_;
        z = z_;
    }

    [[nodiscard]] auto is_right_hand() const noexcept {
        return ocarina::is_right_hand(x, y, z);
    }

    template<typename TVec>
    [[nodiscard]] auto to_local(const TVec &world_v) const noexcept {
        return make_float3(dot(world_v, nx()), dot(world_v, ny()), dot(world_v, nz()));
    }

    template<typename TVec>
    [[nodiscard]] auto to_world(const TVec &local_v) const noexcept {
        return nx() * local_v.x + ny() * local_v.y + nz() * local_v.z;
    }

    template<typename U>
    [[nodiscard]] auto operator==(const Frame<U> &frame) const noexcept {
        return frame.x == x && frame.y == y && frame.z == z;
    }

    template<typename U>
    [[nodiscard]] auto operator!=(const Frame<U> &frame) const {
        return !operator==(frame);
    }
};

template<typename TMat>
[[nodiscard]] auto trace(const TMat &m) noexcept {
    return m[0][0] + m[1][1] + m[2][2];
}

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] Frame<T> frame_from_xz(const T &x, const T &z) noexcept {
    return Frame<T>(x, cross(x, z), z);
}

template<typename T>
requires is_vector3_expr_v<T>
[[nodiscard]] Frame<T> frame_from_xy(const T &x, const T &y) noexcept {
    return Frame<T>(x, y, cross(x, y));
}

template<typename T, typename U>
requires is_vector3_expr_v<T> && is_vector3_expr_v<U>
[[nodiscard]] condition_t<bool, T, U> same_hemisphere(const T &w1, const U &w2) noexcept {
    return w1.z * w2.z > 0;
}

template<typename T, typename U, typename V>
requires is_all_vector3_expr_v<T, U, V>
[[nodiscard]] condition_t<bool, T, U, V> same_hemisphere(const T &w1, const U &w2, const V &n) noexcept {
    return dot(w1, n) * dot(w2, n) > 0;
}

}
}// namespace ocarina::geometry

namespace ocarina {

inline namespace geometry {
struct Triangle {
    uint i, j, k;
    Triangle(uint i, uint j, uint k) : i(i), j(j), k(k) {}
    Triangle() = default;
};
}// namespace geometry
}// namespace ocarina
OC_STRUCT(ocarina, Triangle, i, j, k){};

[[nodiscard]] inline bool operator==(const ocarina::Triangle &lhs,
                                     const ocarina::Triangle &rhs) noexcept {
    return lhs.i == rhs.i &&
           lhs.j == rhs.j &&
           lhs.k == rhs.k;
}

namespace ocarina {
struct LineSegment {
    float3 p0;
    float3 p1;
};
}// namespace ocarina

//clang-format off
OC_STRUCT(ocarina, LineSegment, p0, p1){};
//clang-format on

namespace ocarina {
using array_float3 = std::array<float, 3>;
using array_float2 = std::array<float, 2>;
using array_float4 = std::array<float, 4>;
inline namespace geometry {
struct Vertex {
public:
    //todo compress
    array_float3 pos;
    array_float3 n;
    array_float2 uv;
    array_float2 uv2;

public:
    Vertex() = default;
    Vertex(float3 p, float3 n, float2 uv, float2 uv2 = make_float2(0.f))
        : pos{p.x, p.y, p.z},
          n{n.x, n.y, n.z},
          uv{uv.x, uv.y},
          uv2{uv2.x, uv2.y} {}

    void set_position(float3 p) noexcept {
        pos[0] = p[0];
        pos[1] = p[1];
        pos[2] = p[2];
    }

    [[nodiscard]] auto position() const noexcept {
        return make_float3(pos[0], pos[1], pos[2]);
    }

    void set_normal(float3 n_) noexcept {
        n[0] = n_[0];
        n[1] = n_[1];
        n[2] = n_[2];
    }

    [[nodiscard]] auto normal() const noexcept {
        return make_float3(n[0], n[1], n[2]);
    }

    [[nodiscard]] auto tex_coord() const noexcept {
        return make_float2(uv[0], uv[1]);
    }

    [[nodiscard]] auto lightmap_uv() const noexcept {
        return make_float2(uv2[0], uv2[1]);
    }

    void set_lightmap_uv(float2 uv_) noexcept {
        uv2 = {uv_.x, uv_.y};
    }

#define VS_ATTR_OFFSET(attr)                               \
    [[nodiscard]] static size_t attr##_offset() noexcept { \
        return OC_OFFSET_OF(Vertex, attr);                 \
    }
    VS_ATTR_OFFSET(pos)
    VS_ATTR_OFFSET(n)
    VS_ATTR_OFFSET(uv)
    VS_ATTR_OFFSET(uv2)
#undef VS_ATTR_STRIDE_OFFSET
};
}// namespace geometry
}// namespace ocarina

// clang-format off
OC_STRUCT(ocarina,Vertex, pos, n, uv, uv2){
    void set_position(Float3 p) noexcept {
        pos[0] = p[0];
        pos[1] = p[1];
        pos[2] = p[2];
    }

    void set_normal(Float3 n_) noexcept {
        n[0] = n_[0];
        n[1] = n_[1];
        n[2] = n_[2];
    }

    void set_lightmap_uv(Float2 uv_) noexcept {
        uv2[0] = uv_[0];
        uv2[1] = uv_[1];
    }

    [[nodiscard]] auto position() const noexcept {
        return pos.as_vec3();
    }

    [[nodiscard]] auto normal() const noexcept {
        return n.as_vec3();
    }

    [[nodiscard]] auto tex_coord() const noexcept {
        return uv.as_vec2();
    }

    [[nodiscard]] auto lightmap_uv() const noexcept {
        return uv2.as_vec2();
    }
};
// clang-format on