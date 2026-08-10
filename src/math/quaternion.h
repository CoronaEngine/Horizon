//
// Created by Zero on 2024/3/30.
//

#pragma once

#include "math/basic_types.h"
#include "dsl/dsl.h"
#include "geometry.h"

namespace ocarina {

template<EPort p>
class oc_quaternion {
private:
    oc_float4<p> vw_{make_float4(0, 0, 0, 1)};

public:
    oc_quaternion() = default;
    explicit oc_quaternion(oc_float4<p> val) : vw_(val) {}
    explicit oc_quaternion(oc_float3<p> v, oc_float<p> w) : vw_(make_float4(v, w)) {}

    static oc_quaternion<p> from_axis_angle(oc_float3<p> axis, oc_float<p> theta) {
        oc_float<p> half_theta = theta * 0.5f;
        oc_float3<p> v = ocarina::sin(half_theta) * axis;
        oc_float<p> w = ocarina::cos(half_theta);
        return oc_quaternion<p>(v, w);
    }

    static oc_quaternion<p> from_float3x3(oc_float3x3<p> m) {
        oc_quaternion<p> ret;
        float3 v;
        float w;
        float trace = m[0][0] + m[1][1] + m[2][2];
        if (trace > 0.f) {
            // 4w^2 = m[0][0] + m[1][1] + m[2][2] + m[3][3] (but m[3][3] == 1)
            float s = std::sqrt(trace + 1.0f);
            w = s / 2.0f;
            s = 0.5f / s;
            v.x = (m[1][2] - m[2][1]) * s;
            v.y = (m[2][0] - m[0][2]) * s;
            v.z = (m[0][1] - m[1][0]) * s;
        } else {
            const int nxt[3] = {1, 2, 0};
            float q[3];
            int i = 0;
            if (m[1][1] > m[0][0]) i = 1;
            if (m[2][2] > m[i][i]) i = 2;
            int j = nxt[i];
            int k = nxt[j];
            float s = std::sqrt((m[i][i] - (m[j][j] + m[k][k])) + 1.0f);
            q[i] = s * 0.5f;
            if (s != 0.f) s = 0.5f / s;
            w = (m[j][k] - m[k][j]) * s;
            q[j] = (m[i][j] + m[j][i]) * s;
            q[k] = (m[i][k] + m[k][i]) * s;
            v.x = q[0];
            v.y = q[1];
            v.z = q[2];
        }
        return oc_quaternion<p>(v, w);
    }

    static oc_quaternion<p> from_float4x4(oc_float4x4<p> m) {
        return from_float3x3(make_float3x3(m));
    }

    oc_float3x3<p> to_float3x3() const noexcept {
        oc_float<p> xx = v().x * v().x, yy = v().y * v().y, zz = v().z * v().z;
        oc_float<p> xy = v().x * v().y, xz = v().x * v().z, yz = v().y * v().z;
        oc_float<p> wx = v().x * w(), wy = v().y * w(), wz = v().z * w();
        oc_float3x3<p> m;
        m[0][0] = 1 - 2 * (yy + zz);
        m[1][0] = 2 * (xy + wz);
        m[2][0] = 2 * (xz - wy);
        m[0][1] = 2 * (xy - wz);
        m[1][1] = 1 - 2 * (xx + zz);
        m[2][1] = 2 * (yz + wx);
        m[0][2] = 2 * (xz + wy);
        m[1][2] = 2 * (yz - wx);
        m[2][2] = 1 - 2 * (xx + yy);
        return m;
    }
    oc_quaternion &operator+=(const oc_quaternion &q) {
        vw_ += q.vw_;
        return *this;
    }
    oc_float3<p> v() const noexcept {
        return vw_.xyz();
    }
    oc_float<p> w() const noexcept {
        return vw_.w;
    }
    oc_float<p> theta() const noexcept {
        oc_float<p> half_theta = safe_acos(w());
        return half_theta * 2.f;
    }
    oc_float3<p> axis() const noexcept {
        oc_float<p> half_theta = safe_acos(w());
        oc_float<p> s = sin(half_theta);
        return v() / s;
    }
    friend oc_quaternion<p> operator+(const oc_quaternion<p> &q1, const oc_quaternion<p> &q2) {
        oc_quaternion<p> ret = q1;
        return ret += q2;
    }
    oc_quaternion &operator-=(const oc_quaternion &q) {
        vw_ -= q.vw_;
        return *this;
    }
    oc_quaternion operator-() const {
        oc_quaternion ret;
        ret.vw_ = -vw_;
        return ret;
    }
    friend oc_quaternion<p> operator-(const oc_quaternion<p> &q1, const oc_quaternion<p> &q2) {
        oc_quaternion<p> ret = q1;
        return ret -= q2;
    }
    oc_quaternion &operator*=(oc_float<p> f) {
        vw_ *= f;
        return *this;
    }
    oc_quaternion operator*(oc_float<p> f) const {
        oc_quaternion ret = *this;
        ret.v() *= f;
        return ret;
    }
    oc_quaternion &operator/=(oc_float<p> f) {
        vw_ /= f;
        return *this;
    }
    oc_quaternion operator/(oc_float<p> f) const {
        oc_quaternion ret = *this;
        ret.v() /= f;
        return ret;
    }
};

using quaternion = oc_quaternion<H>;
using Quaternion = oc_quaternion<D>;

}// namespace ocarina
