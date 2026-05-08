# -*- coding:utf-8 -*-
import os


def get_type_content(type_name, prefix = "", device_flag = ""):
    scalar_type = f"{prefix}{type_name}"
    vec2 = f"{scalar_type}2"
    vec3 = f"{scalar_type}3"
    vec4 = f"{scalar_type}4"
    mat2 = f"{scalar_type}2x2"
    mat3 = f"{scalar_type}3x3"
    mat4 = f"{scalar_type}4x4"
    uint_type = f"{prefix}uint"
    make_vec2 = f"{prefix}make_{type_name}2"
    make_vec3 = f"{prefix}make_{type_name}3"
    make_mat2 = f"{prefix}make_{type_name}2x2"
    make_mat3 = f"{prefix}make_{type_name}3x3"
    make_mat4 = f"{prefix}make_{type_name}4x4"
    octahedral_content = ""
    if type_name == "float":
        octahedral_content = f"""

[[nodiscard]] {device_flag} inline auto {prefix}octahedral_encode({vec3} n) noexcept {{
    const auto one = {scalar_type}(1.f);
    const auto zero = {scalar_type}(0.f);
    const auto denom = {prefix}abs(n.x) + {prefix}abs(n.y) + {prefix}abs(n.z);
    if (denom == zero) {{
        return {make_vec2}(zero);
    }}
    auto p = {make_vec2}(n.x, n.y) / denom;
    if (n.z < zero) {{
        p = {make_vec2}(
            (one - {prefix}abs(p.y)) * {prefix}sign(p.x),
            (one - {prefix}abs(p.x)) * {prefix}sign(p.y));
    }}
    return p;
}}

[[nodiscard]] {device_flag} inline auto {prefix}octahedral_decode({vec2} p) noexcept {{
    const auto one = {scalar_type}(1.f);
    const auto zero = {scalar_type}(0.f);
    auto n = {make_vec3}(p.x, p.y, one - {prefix}abs(p.x) - {prefix}abs(p.y));
    if (n.z < zero) {{
        const auto x = n.x;
        const auto y = n.y;
        n.x = (one - {prefix}abs(y)) * {prefix}sign(x);
        n.y = (one - {prefix}abs(x)) * {prefix}sign(y);
    }}
    const auto len = {prefix}length(n);
    return len == zero ? {make_vec3}(zero, zero, one) : n / len;
}}

[[nodiscard]] {device_flag} inline auto {prefix}octahedral_encode01({vec3} n) noexcept {{
    const auto one = {scalar_type}(1.f);
    return {prefix}octahedral_encode(n) * {scalar_type}(0.5f) + {make_vec2}(one * {scalar_type}(0.5f));
}}

[[nodiscard]] {device_flag} inline auto {prefix}octahedral_decode01({vec2} p) noexcept {{
    return {prefix}octahedral_decode(p * {scalar_type}(2.f) - {make_vec2}({scalar_type}(1.f)));
}}

[[nodiscard]] {device_flag} inline auto {prefix}oct32_encode({vec3} n) noexcept {{
    const auto scale = {scalar_type}(65535.f);
    const auto encoded = {prefix}octahedral_encode01(n);
    const auto x = static_cast<{uint_type}>({prefix}clamp(encoded.x * scale + {scalar_type}(0.5f), {scalar_type}(0.f), scale));
    const auto y = static_cast<{uint_type}>({prefix}clamp(encoded.y * scale + {scalar_type}(0.5f), {scalar_type}(0.f), scale));
    return x | (y << 16u);
}}

[[nodiscard]] {device_flag} inline auto {prefix}oct32_decode({uint_type} packed) noexcept {{
    const auto inv_scale = {scalar_type}(1.f / 65535.f);
    const auto x = static_cast<{scalar_type}>(packed & {uint_type}(0xffffu)) * inv_scale;
    const auto y = static_cast<{scalar_type}>(packed >> 16u) * inv_scale;
    return {prefix}octahedral_decode01({make_vec2}(x, y));
}}
"""
    return f"""
[[nodiscard]] {device_flag} inline auto {prefix}face_forward({vec3} n, {vec3} i, {vec3} n_ref) noexcept {{ return {prefix}dot(n_ref, i) < 0.0f ? n : -n; }}

[[nodiscard]] {device_flag} inline auto {prefix}face_forward({vec3} v1, {vec3} v2) noexcept {{ return {prefix}dot(v1, v2) > 0 ? v1 : -v1; }}

[[nodiscard]] {device_flag} inline auto {prefix}determinant(const {mat2} m) noexcept {{
    return m[0][0] * m[1][1] - m[1][0] * m[0][1];
}}

[[nodiscard]] {device_flag} inline auto {prefix}determinant(const {mat3} m) noexcept {{// from GLM
    return m[0].x * (m[1].y * m[2].z - m[2].y * m[1].z) - m[1].x * (m[0].y * m[2].z - m[2].y * m[0].z) + m[2].x * (m[0].y * m[1].z - m[1].y * m[0].z);
}}

[[nodiscard]] {device_flag} inline auto {prefix}determinant(const {mat4} m) noexcept {{// from GLM
    const auto coef00 = m[2].z * m[3].w - m[3].z * m[2].w;
    const auto coef02 = m[1].z * m[3].w - m[3].z * m[1].w;
    const auto coef03 = m[1].z * m[2].w - m[2].z * m[1].w;
    const auto coef04 = m[2].y * m[3].w - m[3].y * m[2].w;
    const auto coef06 = m[1].y * m[3].w - m[3].y * m[1].w;
    const auto coef07 = m[1].y * m[2].w - m[2].y * m[1].w;
    const auto coef08 = m[2].y * m[3].z - m[3].y * m[2].z;
    const auto coef10 = m[1].y * m[3].z - m[3].y * m[1].z;
    const auto coef11 = m[1].y * m[2].z - m[2].y * m[1].z;
    const auto coef12 = m[2].x * m[3].w - m[3].x * m[2].w;
    const auto coef14 = m[1].x * m[3].w - m[3].x * m[1].w;
    const auto coef15 = m[1].x * m[2].w - m[2].x * m[1].w;
    const auto coef16 = m[2].x * m[3].z - m[3].x * m[2].z;
    const auto coef18 = m[1].x * m[3].z - m[3].x * m[1].z;
    const auto coef19 = m[1].x * m[2].z - m[2].x * m[1].z;
    const auto coef20 = m[2].x * m[3].y - m[3].x * m[2].y;
    const auto coef22 = m[1].x * m[3].y - m[3].x * m[1].y;
    const auto coef23 = m[1].x * m[2].y - m[2].x * m[1].y;
    const auto fac0 = {vec4}(coef00, coef00, coef02, coef03);
    const auto fac1 = {vec4}(coef04, coef04, coef06, coef07);
    const auto fac2 = {vec4}(coef08, coef08, coef10, coef11);
    const auto fac3 = {vec4}(coef12, coef12, coef14, coef15);
    const auto fac4 = {vec4}(coef16, coef16, coef18, coef19);
    const auto fac5 = {vec4}(coef20, coef20, coef22, coef23);
    const auto Vec0 = {vec4}(m[1].x, m[0].x, m[0].x, m[0].x);
    const auto Vec1 = {vec4}(m[1].y, m[0].y, m[0].y, m[0].y);
    const auto Vec2 = {vec4}(m[1].z, m[0].z, m[0].z, m[0].z);
    const auto Vec3 = {vec4}(m[1].w, m[0].w, m[0].w, m[0].w);
    const auto inv0 = Vec1 * fac0 - Vec2 * fac1 + Vec3 * fac2;
    const auto inv1 = Vec0 * fac0 - Vec2 * fac3 + Vec3 * fac4;
    const auto inv2 = Vec0 * fac1 - Vec1 * fac3 + Vec3 * fac5;
    const auto inv3 = Vec0 * fac2 - Vec1 * fac4 + Vec2 * fac5;
    const auto sign_a = {vec4}(+1.0f, -1.0f, +1.0f, -1.0f);
    const auto sign_b = {vec4}(-1.0f, +1.0f, -1.0f, +1.0f);
    const auto inv_0 = inv0 * sign_a;
    const auto inv_1 = inv1 * sign_b;
    const auto inv_2 = inv2 * sign_a;
    const auto inv_3 = inv3 * sign_b;
    const auto dot0 = m[0] * {vec4}(inv_0.x, inv_1.x, inv_2.x, inv_3.x);
    return dot0.x + dot0.y + dot0.z + dot0.w;
}}

[[nodiscard]] {device_flag} inline auto {prefix}inverse(const {mat2} m) noexcept {{
    const auto one_over_determinant = 1.0f / (m[0][0] * m[1][1] - m[1][0] * m[0][1]);
    return {make_mat2}(m[1][1] * one_over_determinant,
                            -m[0][1] * one_over_determinant,
                            -m[1][0] * one_over_determinant,
                            +m[0][0] * one_over_determinant);
}}

[[nodiscard]] {device_flag} inline auto {prefix}inverse({mat3} m) noexcept {{// from GLM
    {scalar_type} one_over_determinant = 1.0f / (m[0].x * (m[1].y * m[2].z - m[2].y * m[1].z) -
                                            m[1].x * (m[0].y * m[2].z - m[2].y * m[0].z) +
                                            m[2].x * (m[0].y * m[1].z - m[1].y * m[0].z));
    return {make_mat3}(
        (m[1].y * m[2].z - m[2].y * m[1].z) * one_over_determinant,
        (m[2].y * m[0].z - m[0].y * m[2].z) * one_over_determinant,
        (m[0].y * m[1].z - m[1].y * m[0].z) * one_over_determinant,
        (m[2].x * m[1].z - m[1].x * m[2].z) * one_over_determinant,
        (m[0].x * m[2].z - m[2].x * m[0].z) * one_over_determinant,
        (m[1].x * m[0].z - m[0].x * m[1].z) * one_over_determinant,
        (m[1].x * m[2].y - m[2].x * m[1].y) * one_over_determinant,
        (m[2].x * m[0].y - m[0].x * m[2].y) * one_over_determinant,
        (m[0].x * m[1].y - m[1].x * m[0].y) * one_over_determinant);
}}

[[nodiscard]] {device_flag} inline auto {prefix}inverse(const {mat4} m) noexcept {{// from GLM
    const auto coef00 = m[2].z * m[3].w - m[3].z * m[2].w;
    const auto coef02 = m[1].z * m[3].w - m[3].z * m[1].w;
    const auto coef03 = m[1].z * m[2].w - m[2].z * m[1].w;
    const auto coef04 = m[2].y * m[3].w - m[3].y * m[2].w;
    const auto coef06 = m[1].y * m[3].w - m[3].y * m[1].w;
    const auto coef07 = m[1].y * m[2].w - m[2].y * m[1].w;
    const auto coef08 = m[2].y * m[3].z - m[3].y * m[2].z;
    const auto coef10 = m[1].y * m[3].z - m[3].y * m[1].z;
    const auto coef11 = m[1].y * m[2].z - m[2].y * m[1].z;
    const auto coef12 = m[2].x * m[3].w - m[3].x * m[2].w;
    const auto coef14 = m[1].x * m[3].w - m[3].x * m[1].w;
    const auto coef15 = m[1].x * m[2].w - m[2].x * m[1].w;
    const auto coef16 = m[2].x * m[3].z - m[3].x * m[2].z;
    const auto coef18 = m[1].x * m[3].z - m[3].x * m[1].z;
    const auto coef19 = m[1].x * m[2].z - m[2].x * m[1].z;
    const auto coef20 = m[2].x * m[3].y - m[3].x * m[2].y;
    const auto coef22 = m[1].x * m[3].y - m[3].x * m[1].y;
    const auto coef23 = m[1].x * m[2].y - m[2].x * m[1].y;
    const auto fac0 = {vec4}(coef00, coef00, coef02, coef03);
    const auto fac1 = {vec4}(coef04, coef04, coef06, coef07);
    const auto fac2 = {vec4}(coef08, coef08, coef10, coef11);
    const auto fac3 = {vec4}(coef12, coef12, coef14, coef15);
    const auto fac4 = {vec4}(coef16, coef16, coef18, coef19);
    const auto fac5 = {vec4}(coef20, coef20, coef22, coef23);
    const auto Vec0 = {vec4}(m[1].x, m[0].x, m[0].x, m[0].x);
    const auto Vec1 = {vec4}(m[1].y, m[0].y, m[0].y, m[0].y);
    const auto Vec2 = {vec4}(m[1].z, m[0].z, m[0].z, m[0].z);
    const auto Vec3 = {vec4}(m[1].w, m[0].w, m[0].w, m[0].w);
    const auto inv0 = Vec1 * fac0 - Vec2 * fac1 + Vec3 * fac2;
    const auto inv1 = Vec0 * fac0 - Vec2 * fac3 + Vec3 * fac4;
    const auto inv2 = Vec0 * fac1 - Vec1 * fac3 + Vec3 * fac5;
    const auto inv3 = Vec0 * fac2 - Vec1 * fac4 + Vec2 * fac5;
    const auto sign_a = {vec4}(+1.0f, -1.0f, +1.0f, -1.0f);
    const auto sign_b = {vec4}(-1.0f, +1.0f, -1.0f, +1.0f);
    const auto inv_0 = inv0 * sign_a;
    const auto inv_1 = inv1 * sign_b;
    const auto inv_2 = inv2 * sign_a;
    const auto inv_3 = inv3 * sign_b;
    const auto dot0 = m[0] * {vec4}(inv_0.x, inv_1.x, inv_2.x, inv_3.x);
    const auto dot1 = dot0.x + dot0.y + dot0.z + dot0.w;
    const auto one_over_determinant = 1.0f / dot1;
    return {make_mat4}(inv_0 * one_over_determinant,
                            inv_1 * one_over_determinant,
                            inv_2 * one_over_determinant,
                            inv_3 * one_over_determinant);
}}

{device_flag} inline void {prefix}coordinate_system({vec3} v1, {vec3} &v2, {vec3} &v3) noexcept {{
    v2 = {prefix}select({prefix}abs(v1.x) > {prefix}abs(v1.y),
                {make_vec3}(-v1.z, 0.f, v1.x) / {prefix}sqrt(v1.x * v1.x + v1.z * v1.z),
                {make_vec3}(0.f, v1.z, -v1.y) / {prefix}sqrt(v1.y * v1.y + v1.z * v1.z));
    v3 = {prefix}cross(v1, v2);
}}

{device_flag} inline void {prefix}make_normal_tangent({vec3} N, {vec3} T, {vec3} &a, {vec3} &b){{
    b = {prefix}normalize({prefix}cross(N, T));
    a = {prefix}cross(b, N);
}}
{octahedral_content}
"""


def get_content(prefix = "", device_flag = ""):
    cur_fn = os.path.basename(__file__)
    content = f"""
// this file was generated by {cur_fn}, please do not manually modify 

{get_type_content("float", prefix, device_flag)}
{get_type_content("half", prefix, device_flag)}
"""
    return content

def main():
    curr_dir = os.path.dirname(os.path.realpath(__file__))


    print(curr_dir)
    fn = os.path.join(curr_dir, "common_lib.inl.h")
    string = get_content()
    with open(fn, "w") as f:
        f.write(string)
        f.close()

    fn = os.path.join(curr_dir, "../backends/cuda/builtin/cuda_device_math.h")
    string = get_content("oc_", "__device__")
    with open(fn, "w") as f:
        f.write(string)
        f.close()


if __name__ == "__main__":
    main()