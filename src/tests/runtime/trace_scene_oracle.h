#pragma once

#include "dsl/dsl.h"
#include "math/geometry.h"
#include "rhi/common.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace ocarina::test {

inline void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "runtime parity: " << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

struct Scene {
    array<float3, 6> vertices{
        make_float3(0.f, 0.f, 0.f), make_float3(1.25f, 0.f, 0.f), make_float3(0.f, 1.f, 0.f),
        make_float3(2.f, 0.f, 0.f), make_float3(3.f, 0.f, 0.f), make_float3(2.f, 1.5f, 0.f)};
    array<Triangle, 2> triangles{Triangle(0u, 1u, 2u), Triangle(3u, 4u, 5u)};
    array<float4x4, 3> transforms{
        float4x4(make_float4(1.f, 0.f, 0.f, 0.f), make_float4(0.f, 1.f, 0.f, 0.f),
                 make_float4(0.f, 0.f, 1.f, 0.f), make_float4(-1.6f, -.9f, .2f, 1.f)),
        float4x4(make_float4(.75f, .35f, .15f, 0.f), make_float4(-.4f, 1.3f, -.1f, 0.f),
                 make_float4(.1f, .2f, .8f, 0.f), make_float4(.5f, -.6f, 1.8f, 1.f)),
        float4x4(make_float4(-.9f, .15f, .05f, 0.f), make_float4(.2f, .85f, .1f, 0.f),
                 make_float4(0.f, 0.f, 1.1f, 0.f), make_float4(1.8f, .6f, -.8f, 1.f))};
};

// Independent host triangle intersection. Public barycentrics contain weights
// of vertices 0 and 1, rather than the native traversal weights of 1 and 2.
inline TriangleHit trace_reference(const Scene &scene, const Ray &ray, float *distance = nullptr) {
    TriangleHit hit{};
    float closest = ray.t_max();
    for (uint instance = 0u; instance < scene.transforms.size(); ++instance) {
        for (uint primitive = 0u; primitive < scene.triangles.size(); ++primitive) {
            array<float3, 3> v{};
            for (uint corner = 0u; corner < 3u; ++corner)
                v[corner] = (scene.transforms[instance] * make_float4(scene.vertices[3u * primitive + corner], 1.f)).xyz();
            const auto e1 = v[1] - v[0];
            const auto e2 = v[2] - v[0];
            const auto p = cross(ray.direction(), e2);
            const float determinant = dot(e1, p);
            if (std::abs(determinant) < 1e-8f) continue;
            const float inverse = 1.f / determinant;
            const auto s = ray.origin() - v[0];
            const float u = dot(s, p) * inverse;
            const auto q = cross(s, e1);
            const float v_weight = dot(ray.direction(), q) * inverse;
            const float t = dot(e2, q) * inverse;
            if (u < 0.f || v_weight < 0.f || u + v_weight > 1.f || t < ray.t_min() || t > closest) continue;
            closest = t;
            hit.inst_id = instance;
            hit.prim_id = primitive;
            hit.bary = make_float2(1.f - u - v_weight, u);
        }
    }
    if (distance) *distance = hit.is_miss() ? 0.f : closest;
    return hit;
}

inline vector<Ray> make_test_rays(uint3 dim) {
    vector<Ray> rays(dim.x * dim.y * dim.z);
    for (uint z = 0u; z < dim.z; ++z)
        for (uint y = 0u; y < dim.y; ++y)
            for (uint x = 0u; x < dim.x; ++x) {
                uint i = (z * dim.y + y) * dim.x + x;
                float3 origin = make_float3(-3.1f + (float(x) + .37f) * .43f,
                                             -2.2f + (float(y) + .29f) * .41f,
                                             z == 0u ? 6.f : -3.f);
                float3 direction = z == 0u ? make_float3(.03f, -.02f, -1.f) : make_float3(.01f, .04f, 1.f);
                rays[i] = make_ray(origin, direction, i % 11u == 0u ? 3.f : 20.f);
                rays[i].org_min.w = i % 7u == 0u ? 6.f : .001f;
            }
    return rays;
}

inline bool has_miss_payload(const TriangleHit &hit) {
    return hit.inst_id == ~0u && hit.prim_id == ~0u && hit.bary.x == 0.f && hit.bary.y == 0.f;
}

inline void validate_scene(const Scene &scene, uint3 dim, const vector<Ray> &rays,
                    const vector<TriangleHit> &hits, const vector<uint4> &metadata) {
    uint hit_count = 0u;
    array<bool, 6> seen{};
    for (uint i = 0u; i < rays.size(); ++i) {
        const auto expected = trace_reference(scene, rays[i]);
        const auto actual = hits[i];
        uint3 index = make_uint3(i % dim.x, (i / dim.x) % dim.y, i / (dim.x * dim.y));
        const auto position = metadata[2u * i];
        const auto shape = metadata[2u * i + 1u];
        require(position.x == index.x && position.y == index.y && position.z == index.z && position.w == i,
                "raygen dispatch index or configured-grid coverage differs");
        require(shape.x == dim.x && shape.y == dim.y && shape.z == dim.z,
                "raygen dispatch dimensions differ");
        require(shape.w == uint(!expected.is_miss()), "trace_occlusion differs from closest-hit visibility or ray range");
        const bool hit_matches = expected.is_miss() ? has_miss_payload(actual) :
            actual.inst_id == expected.inst_id && actual.prim_id == expected.prim_id &&
            std::abs(actual.bary.x - expected.bary.x) <= 3e-4f &&
            std::abs(actual.bary.y - expected.bary.y) <= 3e-4f;
        if (!hit_matches) {
            std::cerr << "ray=" << i << " actual=" << actual.inst_id << ',' << actual.prim_id
                      << ',' << actual.bary.x << ',' << actual.bary.y
                      << " expected=" << expected.inst_id << ',' << expected.prim_id
                      << ',' << expected.bary.x << ',' << expected.bary.y << std::endl;
            require(false, "closest hit instance/primitive/barycentrics differ from transformed host triangles");
        }
        if (!expected.is_miss()) {
            ++hit_count;
            seen[expected.inst_id * 2u + expected.prim_id] = true;
        }
    }
    require(hit_count > 20u && rays.size() - hit_count > 20u, "ray scene needs both hits and misses");
    for (bool visible : seen) require(visible, "ray scene must cover every instance and primitive");
}

}// namespace ocarina::test
