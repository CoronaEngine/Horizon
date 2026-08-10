//
// Created by Zero on 15/09/2022.
//

#pragma once

#include "math/basic_types.h"
#include "dsl/dsl.h"
#include "core/concepts.h"
#include "math/box.h"
#include "quaternion.h"

namespace ocarina {

inline namespace transform {

template<EPort p = D>
[[nodiscard]] constexpr oc_float4x4<p> translation(const oc_float3<p> &t) noexcept {
    return make_float4x4(
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        t.x, t.y, t.z, 1.f);
}

template<EPort p = D>
[[nodiscard]] constexpr oc_float4x4<p> translation(const oc_float<p> &x, const oc_float<p> &y, const oc_float<p> &z) noexcept {
    return translation<p>(make_float3(x, y, z));
}

template<EPort p = D>
[[nodiscard]] constexpr oc_float4x4<p> scale(const oc_float3<p> &s) noexcept {
    return make_float4x4(
        s.x, 0.f, 0.f, 0.f,
        0.f, s.y, 0.f, 0.f,
        0.f, 0.f, s.z, 0.f,
        0.f, 0.f, 0.f, 1.f);
}

template<EPort p = D>
[[nodiscard]] constexpr oc_float4x4<p> scale(const oc_float<p> &x, const oc_float<p> &y,
                                             const oc_float<p> &z) noexcept {
    return scale<p>(make_float3(x, y, z));
}

template<EPort p = D>
[[nodiscard]] constexpr oc_float4x4<p> scale(const oc_float<p> &s) noexcept {
    return scale<p>(make_float3(s));
}

template<EPort p = D>
[[nodiscard]] inline oc_float4x4<p> perspective(oc_float<p> fov_y, const oc_float<p> &z_near,
                                                const oc_float<p> &z_far, oc_bool<p> radian = false) {
    fov_y = ocarina::select(radian, fov_y, radians(fov_y));
    oc_float<p> inv_tan = 1 / tan(fov_y / 2.f);
    oc_float4x4<p> mat = make_float4x4(
        inv_tan, 0, 0, 0,
        0, inv_tan, 0, 0,
        0, 0, z_far / (z_far - z_near), 1,
        0, 0, -z_far * z_near / (z_far - z_near), 0);
    return mat;
}

template<EPort p = D>
[[nodiscard]] oc_float4x4<p> rotation(const oc_float3<p> &axis, oc_float<p> angle,
                                      oc_bool<p> radian = false) noexcept {
    angle = ocarina::select(radian, angle, radians(angle));
    oc_float<p> c = cos(angle);
    oc_float<p> s = sin(angle);
    oc_float3<p> a = normalize(axis);
    oc_float3<p> t = (1.f - c) * a;
    return make_float4x4(
        c + t.x * a.x, t.x * a.y + s * a.z, t.x * a.z - s * a.y, 0.0f,
        t.y * a.x - s * a.z, c + t.y * a.y, t.y * a.z + s * a.x, 0.0f,
        t.z * a.x + s * a.y, t.z * a.y - s * a.x, c + t.z * a.z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
}

template<EPort p = D>
[[nodiscard]] oc_float4x4<p> rotation_x(const oc_float<p> &angle, oc_bool<p> radian = false) noexcept {
    return rotation<p>(make_float3(1, 0, 0), angle, radian);
}

template<EPort p = D>
[[nodiscard]] oc_float4x4<p> rotation_y(const oc_float<p> &angle, oc_bool<p> radian = false) noexcept {
    return rotation<p>(make_float3(0, 1, 0), angle, radian);
}

template<EPort p = D>
[[nodiscard]] oc_float4x4<p> rotation_z(const oc_float<p> &angle, oc_bool<p> radian = false) noexcept {
    return rotation<p>(make_float3(0, 0, 1), angle, radian);
}

template<EPort p = D>
[[nodiscard]] oc_float4x4<p> TRS(const oc_float3<p> &t, const oc_float4<p> &r,
                                 const oc_float3<p> &s) noexcept {
    oc_float4x4<p> T = translation<p>(t);
    oc_float4x4<p> R = rotation<p>(make_float3(r), r.w, false);
    oc_float4x4<p> S = scale<p>(s);
    return T * R * S;
}

template<EPort p = D>
[[nodiscard]] oc_float4x4<p> look_at(const oc_float3<p> &eye, const oc_float3<p> &target_pos,
                                     oc_float3<p> up) noexcept {
    oc_float3<p> fwd = normalize(target_pos - eye);
    oc_float3<p> right = normalize(cross(fwd, up));
    up = normalize(cross(right, fwd));
    return make_float4x4(
        right.x, right.y, right.z, 0.f,
        up.x, up.y, up.z, 0.f,
        fwd.x, fwd.y, fwd.z, 0.f,
        eye.x, eye.y, eye.z, 1.f);
}

template<EPort p = D>
[[nodiscard]] oc_float3<p> transform_vector(const oc_float4x4<p> &m, const oc_float3<p> &vec) noexcept {
    oc_float3x3<p> mat3x3 = make_float3x3(m);
    return mat3x3 * vec;
}

template<EPort p = D>
[[nodiscard]] oc_float3<p> transform_point(const oc_float4x4<p> &m, const oc_float3<p> &point) noexcept {
    oc_float4<p> homo_point = make_float4(point, 1.f);
    return make_float3(m * homo_point);
}

template<EPort p = D>
[[nodiscard]] oc_float3<p> transform_normal(const oc_float4x4<p> &m, const oc_float3<p> &normal) noexcept {
    return make_float3(transpose(inverse(m)) * make_float4(normal, 0.f));
}

template<EPort p = D>
[[nodiscard]] var_t<Ray, p> transform_ray(const oc_float4x4<p> &m, var_t<Ray, p> ray) noexcept {
    ray.org_min = make_float4(transform_point(m, ray.org_min.xyz()), ray.org_min.w);
    ray.dir_max = make_float4(transform_vector(m, ray.dir_max.xyz()), ray.dir_max.w);
    return ray;
}

inline void decompose(float4x4 m, float3 *T, quaternion *quat, float3 *s) noexcept {
    T->x = m[3][0];
    T->y = m[3][1];
    T->z = m[3][2];
    float4x4 M = m;

    for (int i = 0; i < 3; ++i) {
        M[i][3] = M[3][i] = 0.f;
    }
    M[3][3] = 1.f;

    float norm = 1;
    int count = 0;
    float4x4 R = M;
    do {
        float4x4 Rnext;
        float4x4 Rit = inverse(transpose(R));
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                Rnext[i][j] = 0.5f * (R[i][j] + Rit[i][j]);

        norm = 0;
        for (int i = 0; i < 3; ++i) {
            float n = std::abs(R[i][0] - Rnext[i][0]) +
                      std::abs(R[i][1] - Rnext[i][1]) +
                      std::abs(R[i][2] - Rnext[i][2]);
            norm = std::max(norm, n);
        }
        R = Rnext;
    } while (++count < 100 && norm > 0.0001f);
    *quat = quaternion ::from_float4x4(R);
    float4x4 S = inverse(R) * M;
    *s = make_float3(S[0][0], S[1][1], S[2][2]);
}

[[nodiscard]] inline Box3f transform_box(float4x4 mat, const Box3f &b) noexcept {
    float3 minPoint = make_float3(mat[3][0], mat[3][1], mat[3][2]);
    float3 maxPoint = minPoint;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            float e = mat[j][i];
            float p1 = e * b.lower[j];
            float p2 = e * b.upper[j];
            if (p1 > p2) {
                minPoint[i] += p2;
                maxPoint[i] += p1;
            } else {
                minPoint[i] += p1;
                maxPoint[i] += p2;
            }
        }
    }
    return {minPoint, maxPoint};
}

}// namespace transform

template<typename TMat>
requires is_matrix4_v<expr_value_t<TMat>>
struct Transform {
private:
    TMat _mat;
    static constexpr auto port = port_v<TMat>;

public:
    Transform() = default;
    explicit Transform(const TMat &mat) : _mat(mat) {}
    [[nodiscard]] TMat mat4x4() const noexcept { return _mat; }
    [[nodiscard]] TMat inv_mat4x4() const noexcept { return inverse(_mat); }
    [[nodiscard]] oc_float3x3<port> mat3x3() const noexcept { return make_float3x3(_mat); }
    [[nodiscard]] oc_float3x3<port> inv_mat3x3() const noexcept { return inverse(mat3x3()); }
    template<typename M>
    [[nodiscard]] auto operator*(const Transform<M> &other) const noexcept { return Transform(_mat * other._mat); }
    template<typename T>
    [[nodiscard]] auto apply_vector(const T &vec) noexcept { return transform_vector<port_v<TMat, T>>(_mat, vec); }
    template<typename T>
    [[nodiscard]] auto apply_point(const T &point) noexcept { return transform_point<port_v<TMat, T>>(_mat, point); }
    template<typename T>
    [[nodiscard]] auto apply_normal(const T &normal) noexcept { return transform_normal<port_v<TMat, T>>(_mat, normal); }
    template<typename T>
    [[nodiscard]] auto apply_ray(T &&ray) noexcept { return transform_ray<port_v<TMat, T>>(_mat, OC_FORWARD(ray)); }
};

}// namespace ocarina