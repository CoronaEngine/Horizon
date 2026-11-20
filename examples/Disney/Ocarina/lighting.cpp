#include "../scene.h"
#include "lighting.h"
#include <bit>
#include <cstring>
#include <stdexcept>

namespace disney_rt {
using namespace ocarina;

Environment load_environment(Device& device, const char* filename) {
    const auto dds = disney::read_cubemap(filename);
    if (!std::has_single_bit(dds.size) || dds.mip_count > std::bit_width(dds.size))
        throw std::runtime_error("Expected a power-of-two environment cubemap");
    const uint width = 6 * dds.size, height = 2 * dds.size;
    vector<uint64_t> texels(size_t(width) * height); // one RGBA16F texel per word
    size_t offset = 0;
    for (uint face = 0; face < 6; ++face) {
        for (uint mip = 0; mip < dds.mip_count; ++mip) {
            const uint side = dds.size >> mip;
            const uint top = 2 * (dds.size - side);
            for (uint y = 0; y < side; ++y) {
                const size_t bytes = size_t(side) * sizeof(uint64_t);
                if (offset + bytes > dds.payload.size())
                    throw std::runtime_error("Truncated environment cubemap");
                std::memcpy(&texels[size_t(top + y) * width + face * side], dds.payload.data() + offset, bytes);
                offset += bytes;
            }
        }
    }
    auto atlas = device.create_texture2d(make_uint2(width, height), PixelStorage::HALF4);
    atlas.upload_immediately(texels.data());
    return {ocarina::move(atlas), dds.size, dds.mip_count};
}

Float3 sample_environment(const Environment& env, Float3 d, Float mip) {
    Float3 axis = abs(d);
    Float major = max(axis.x, max(axis.y, axis.z));
    Float face;
    Float2 uv;
    $if (axis.x == major) {
        face = select(d.x > 0.f, 0.f, 1.f);
        uv = make_float2(select(d.x > 0.f, -d.z, d.z), -d.y);
    }
    $else {
        $if (axis.y == major) {
            face = select(d.y > 0.f, 2.f, 3.f);
            uv = make_float2(d.x, select(d.y > 0.f, d.z, -d.z));
        }
        $else {
            face = select(d.z > 0.f, 4.f, 5.f);
            uv = make_float2(select(d.z > 0.f, d.x, -d.x), -d.y);
        };
    };
    uv = .5f * (uv / major + 1.f);
    auto sample_level = [&](Float level) -> Float3 {
        Float side = float(env.face_size) * exp2(-level);
        Float2 xy = clamp(uv * side, make_float2(.5f), make_float2(side - .5f));
        xy += make_float2(face * side, 2.f * (float(env.face_size) - side));
        return env.atlas.sample(4, xy / make_float2(6.f * env.face_size, 2.f * env.face_size)).as_vec4().xyz();
    };
    Float level = clamp(mip, 0.f, float(env.levels - 1));
    Float lo = floor(level);
    Float3 first = sample_level(lo);
    Float3 second = sample_level(min(lo + 1.f, float(env.levels - 1)));
    return pow(abs(lerp(level - lo, first, second)), make_float3(2.2f));
}

Float3 direct_brdf(Float3 n, Float3 v, Float3 l, Float3 base, Float3 spec, Float4 m0, Float4 m1, Float4 m2,
                   Float gloss) {
    // Packing matches sphere_parameters in Disney/scene.cpp.
    Float metallic = m0.w, roughness = max(.001f, m1.x), subsurface = m1.w;
    Float anisotropy = m2.x, sheen_strength = m2.y, sheen_tint = m2.z, clearcoat = m2.w;
    constexpr float pi = 3.14159265f;
    Float3 h = normalize(v + l);
    Float nl = max(dot(n, l), 0.f), nv = max(dot(n, v), 0.f), nh = max(dot(n, h), 0.f);
    Float lh = clamp(dot(l, h), 0.f, 1.f);
    Float fl = pow(1.f - nl, 5.f), fv = pow(1.f - nv, 5.f), fh = pow(1.f - lh, 5.f);
    Float fd90 = .5f + 2.f * lh * lh * roughness;
    Float fd = lerp(fl, 1.f, fd90) * lerp(fv, 1.f, fd90);
    Float fss90 = lh * lh * roughness;
    Float fss = lerp(fl, 1.f, fss90) * lerp(fv, 1.f, fss90);
    Float ss = 1.25f * (fss * (1.f / max(nl + nv, .0001f) - .5f) + .5f);
    Float3 tint = base / max(dot(base, make_float3(.3f, .6f, .1f)), .0001f);
    Float3 sheen = fh * sheen_strength * lerp(sheen_tint, make_float3(1.f), tint);
    Float3 diffuse = (1.f - metallic) * (base / pi * lerp(subsurface, fd, ss) + sheen);

    Float3 tangent = cross(n, make_float3(0.f, 1.f, 0.f));
    $if (dot(tangent, tangent) < .0001f) {
        tangent = cross(n, make_float3(1.f, 0.f, 0.f));
    };
    tangent = normalize(tangent);
    Float3 bitangent = cross(n, tangent);
    Float aspect = sqrt(1.f - .9f * anisotropy);
    Float ax = max(.001f, roughness * roughness / aspect), ay = max(.001f, roughness * roughness * aspect);
    Float q = sqr(dot(h, tangent) / ax) + sqr(dot(h, bitangent) / ay) + nh * nh;
    Float distribution = 1.f / (pi * ax * ay * q * q);
    auto smith = [&](Float3 w, Float nw) {
        return 1.f / max(.0001f, nw + sqrt(sqr(dot(w, tangent) * ax) + sqr(dot(w, bitangent) * ay) + nw * nw));
    };
    Float3 reflection = lerp(fh, spec, make_float3(1.f)) * distribution * smith(v, nv) * smith(l, nl);

    Float a2 = sqr(lerp(gloss, .1f, .001f));
    Float coat_d = (a2 - 1.f) / (pi * log(a2) * (1.f + (a2 - 1.f) * nh * nh));
    auto coat_g = [](Float nw) { return 1.f / (nw + sqrt(.0625f + .9375f * nw * nw)); };
    Float coat = .25f * clearcoat * coat_d * lerp(fh, .04f, 1.f) * coat_g(nl) * coat_g(nv);
    return diffuse + reflection + coat;
}
} // namespace disney_rt
