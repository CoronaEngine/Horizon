#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "math/geometry.h"
#include "rhi/common.h"
#include "rhi/context.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

using namespace ocarina;

namespace {
void require(bool condition, const char *message) {
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
TriangleHit trace_reference(const Scene &scene, const Ray &ray) {
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
    return hit;
}

vector<Ray> make_test_rays(uint3 dim) {
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

void validate_scene(const Scene &scene, uint3 dim, const vector<Ray> &rays,
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
        if (actual.is_miss() != expected.is_miss() ||
            (!expected.is_miss() && (actual.inst_id != expected.inst_id || actual.prim_id != expected.prim_id ||
             std::abs(actual.bary.x - expected.bary.x) > 3e-4f || std::abs(actual.bary.y - expected.bary.y) > 3e-4f))) {
            std::cerr << "ray=" << i << " actual=" << actual.inst_id << ',' << actual.prim_id
                      << ',' << actual.bary.x << ',' << actual.bary.y
                      << " expected=" << expected.inst_id << ',' << expected.prim_id
                      << ',' << expected.bary.x << ',' << expected.bary.y << std::endl;
            require(false, "closest hit instance/primitive/barycentrics differ from transformed host triangles");
        }
        // The CUDA miss path does not define primitive/barycentric payloads.
        if (!expected.is_miss()) {
            ++hit_count;
            seen[expected.inst_id * 2u + expected.prim_id] = true;
        }
    }
    require(hit_count > 20u && rays.size() - hit_count > 20u, "ray scene needs both hits and misses");
    for (bool visible : seen) require(visible, "ray scene must cover every instance and primitive");
}

struct DelayedGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered{}, released{};
    std::atomic<bool> expired{};

    void wait() {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        if (!changed.wait_for(lock, std::chrono::seconds(10), [&] { return released; })) expired = true;
    }
    void release_when_entered() {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, std::chrono::seconds(10), [&] { return entered; })) expired = true;
        lock.unlock();
        // Do not require pageable CUDA uploads/commit to be nonblocking.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        lock.lock();
        released = true;
        lock.unlock();
        changed.notify_all();
    }
};

void test_ray_scene_and_clear(Device &device) {
    Scene scene;
    const auto dim = make_uint3(16u, 11u, 2u);
    const auto rays = make_test_rays(dim);
    auto vertices = device.create_buffer<float3>(scene.vertices.size());
    auto triangles = device.create_buffer<Triangle>(scene.triangles.size());
    auto ray_buffer = device.create_buffer<Ray>(rays.size());
    auto hit_buffer = device.create_buffer<TriangleHit>(rays.size());
    auto metadata_buffer = device.create_buffer<uint4>(rays.size() * 2u);
    auto stream = device.create_stream();
    stream << vertices.upload(scene.vertices.data()) << triangles.upload(scene.triangles.data())
           << ray_buffer.upload(rays.data()) << synchronize() << commit();
    auto accel = device.create_accel(FAST_UPDATE);
    for (const auto &transform : scene.transforms) {
        auto mesh = device.create_mesh(vertices.view(), triangles.view(), FAST_TRACE, DISABLE_ANYHIT);
        stream << mesh.build_bvh() << commit();
        accel.add_instance(ocarina::move(mesh), transform);
    }
    stream << accel.build_bvh() << synchronize() << commit();

    Kernel kernel = [&accel](BufferVar<Ray> input, BufferVar<TriangleHit> hits, BufferVar<uint4> metadata) {
        Uint i = dispatch_id();
        Var<Ray> ray = input.read(i);
        hits.write(i, accel.trace_closest(ray));
        metadata.write(2u * i, make_uint4(dispatch_idx(), i));
        metadata.write(2u * i + 1u, make_uint4(dispatch_dim(), cast<uint>(accel.trace_occlusion(ray))));
    };
    // OptiX raygen ignores the configured compute grid. Vulkan ray queries must
    // still cover every requested pixel, including partial groups in 3D.
    kernel.function()->configure(make_uint3(1u), make_uint3(4u, 3u, 2u));
    auto shader = device.compile(kernel, "runtime-parity-transformed-ray-scene");
    vector<TriangleHit> hits(rays.size());
    vector<uint4> metadata(rays.size() * 2u);
    auto render = [&] {
        stream << metadata_buffer.byte_set(0xcdu)
               << shader(ray_buffer, hit_buffer, metadata_buffer).dispatch(dim)
               << hit_buffer.download(hits.data()) << metadata_buffer.download(metadata.data())
               << synchronize() << commit();
        validate_scene(scene, dim, rays, hits, metadata);
    };
    render();
    scene.transforms[1][3].x -= 1.4f;
    scene.transforms[1][3].z += .6f;
    scene.transforms[2][0].x = -.6f;
    for (uint i = 0u; i < scene.transforms.size(); ++i) accel.set_transform(i, scene.transforms[i]);
    stream << accel.update_bvh() << synchronize() << commit();
    require(accel.last_build_action() == Accel::BuildAction::UPDATE, "transform-only ray scene update must refit");
    render();

    // The clear test must observe this dispatch's writes, never results left by
    // the preceding refit render or a download that did not run.
    stream << hit_buffer.byte_set(0xcdu) << metadata_buffer.byte_set(0xcdu)
           << synchronize() << commit();
    for (auto &hit : hits) {
        hit.inst_id = 0xcdcdcdcdu;
        hit.prim_id = 0xcdcdcdcdu;
        hit.bary = make_float2(-1234.5f);
    }
    for (auto &value : metadata) value = make_uint4(0xcdcdcdcdu);

    DelayedGate gate;
    std::thread releaser([&] { gate.release_when_entered(); });
    stream << [&] { gate.wait(); }
           << shader(ray_buffer, hit_buffer, metadata_buffer).dispatch(dim)
           << hit_buffer.download(hits.data()) << metadata_buffer.download(metadata.data()) << commit();
    // The resource remains a valid Accel object, but its internal AS storage is
    // cleared while committed work still refers to its previous handle.
    accel.clear();
    releaser.join();
    stream << synchronize() << commit();
    require(!gate.expired.load(), "clear of an in-flight acceleration structure deadlocked");
    require(accel.mesh_num() == 0u && accel.handle() == 0u && accel.last_build_action() == Accel::BuildAction::NONE,
            "cleared acceleration structure retained state");
    validate_scene(scene, dim, rays, hits, metadata);
}

void test_async_compile_and_stream_destruction(Device &device) {
    constexpr uint count = 37u;
    auto intermediate = device.create_buffer<uint>(count);
    auto output = device.create_buffer<uint>(count);
    Kernel first_kernel = [](BufferVar<uint> out, Uint seed) { out.write(dispatch_id(), seed + dispatch_id() * 17u); };
    Kernel second_kernel = [](BufferVar<uint> in, BufferVar<uint> out, Uint mask) {
        Uint i = dispatch_id();
        out.write(i, in.read(i) ^ mask);
    };
    auto first_future = device.async_compile(ocarina::move(first_kernel), "runtime-parity-async-first");
    auto second_future = device.async_compile(ocarina::move(second_kernel), "runtime-parity-async-second");
    const auto &first = first_future.get();
    const auto &second = second_future.get();
    array<uint, count> values{};
    std::atomic<uint> callbacks{};
    for (uint round = 0u; round < 3u; ++round) {
        {
            auto stream = device.create_stream();
            stream << first(intermediate, 301u + round).dispatch(count) << commit();
            stream << second(intermediate, output, 0x9a5100ffu + round).dispatch(count)
                   << output.download(values.data()) << [&] { ++callbacks; } << commit();
            // Stream destruction is the only completion boundary here.
        }
        require(callbacks.load() == round + 1u, "stream destruction did not drain committed host callbacks");
        for (uint i = 0u; i < count; ++i)
            require(values[i] == ((301u + round + i * 17u) ^ (0x9a5100ffu + round)),
                    "asynchronously compiled shaders or stream destruction lost submitted work");
    }
}
}

int main() {
    auto device = RHIContext::instance().create_device(test_backend_name());
    device.init_rtx();
    test_ray_scene_and_clear(device);
    test_async_compile_and_stream_destruction(device);
    std::cout << "extended runtime parity checks passed" << std::endl;
}
