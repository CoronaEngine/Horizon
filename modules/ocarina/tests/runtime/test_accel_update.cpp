//
// Created by GitHub Copilot.
//

#include "core/stl.h"
#include "dsl/dsl.h"
#include "math/transform.h"
#include "rhi/context.h"
#include "rhi/common.h"
#include <cmath>
#include <iostream>

using namespace ocarina;

namespace {

void expect(bool cond, const char *message) {
    if (!cond) {
        std::cerr << "[test-accel-update] " << message << std::endl;
        std::exit(1);
    }
}

void expect_near(float actual, float expected, float eps, const char *message) {
    expect(std::abs(actual - expected) <= eps, message);
}

auto get_cube(float x = 1.f, float y = 1.f, float z = 1.f) {
    x *= 0.5f;
    y *= 0.5f;
    z *= 0.5f;
    auto vertices = vector<float3>{
        float3(-x, -y, z),
        float3(x, -y, z),
        float3(-x, y, z),
        float3(x, y, z),
        float3(-x, y, -z),
        float3(x, y, -z),
        float3(-x, -y, -z),
        float3(x, -y, -z),
        float3(-x, y, z),
        float3(x, y, z),
        float3(-x, y, -z),
        float3(x, y, -z),
        float3(-x, -y, z),
        float3(x, -y, z),
        float3(-x, -y, -z),
        float3(x, -y, -z),
        float3(x, -y, z),
        float3(x, y, z),
        float3(x, y, -z),
        float3(x, -y, -z),
        float3(-x, -y, z),
        float3(-x, y, z),
        float3(-x, y, -z),
        float3(-x, -y, -z),
    };
    auto triangles = vector<Triangle>{
        Triangle(0, 1, 3),
        Triangle(0, 3, 2),
        Triangle(6, 5, 7),
        Triangle(4, 5, 6),
        Triangle(10, 9, 11),
        Triangle(8, 9, 10),
        Triangle(13, 14, 15),
        Triangle(13, 12, 14),
        Triangle(18, 17, 19),
        Triangle(17, 16, 19),
        Triangle(21, 22, 23),
        Triangle(20, 21, 23),
    };
    return ocarina::make_pair(vertices, triangles);
}

RHIMesh create_cube_mesh(Device &device, Stream &stream) {
    auto [vertices, triangles] = get_cube();
    Buffer vertex_buffer = device.create_buffer<float3>(vertices.size(), "test_accel_update_vertices");
    Buffer triangle_buffer = device.create_buffer<Triangle>(triangles.size(), "test_accel_update_triangles");
    stream << vertex_buffer.upload_sync(vertices.data());
    stream << triangle_buffer.upload_sync(triangles.data());
    RHIMesh mesh = device.create_mesh(vertex_buffer.view(), triangle_buffer.view(), FAST_TRACE, DISABLE_ANYHIT);
    stream << mesh.build_bvh();
    stream << synchronize() << commit();
    return mesh;
}

uint trace_center_hit(Device &device, const Accel &accel) {
    Stream stream = device.create_stream();
    Buffer<uint> result = device.create_buffer<uint>(1, "test_accel_update_hit");
    Kernel kernel = [&accel](BufferVar<uint> output) {
        Var<float3> origin = make_float3(0.f, 0.f, -5.f);
        Var<float3> direction = make_float3(0.f, 0.f, 1.f);
        TriangleHitVar hit = accel.trace_closest(make_ray(origin, direction));
        $if(hit->is_miss()) {
            output.write(0u, 0u);
        }
        $else {
            output.write(0u, 1u);
        };
    };
    auto shader = device.compile(kernel, "test_accel_update_trace");
    uint host = 0u;
    stream << shader(result).dispatch(1u);
    stream << result.download(&host);
    stream << synchronize() << commit();
    return host;
}

TriangleHit trace_hit(Device &device, const Accel &accel, float3 origin, float3 direction,
                      float t_max = ray_t_max) {
    Stream stream = device.create_stream();
    Buffer<TriangleHit> result = device.create_buffer<TriangleHit>(1, "test_accel_update_trace_hit");
    Kernel kernel = [&accel, origin, direction, t_max](BufferVar<TriangleHit> output) {
        Var<float3> ray_origin = make_float3(origin.x, origin.y, origin.z);
        Var<float3> ray_direction = make_float3(direction.x, direction.y, direction.z);
        output.write(0u, accel.trace_closest(make_ray(ray_origin, ray_direction, t_max)));
    };
    auto shader = device.compile(kernel, "test_accel_update_trace_hit_detail");
    TriangleHit host{};
    stream << shader(result).dispatch(1u);
    stream << result.download(&host);
    stream << synchronize() << commit();
    return host;
}

void expect_hit(const TriangleHit &hit, uint inst_id, uint prim_id, float2 bary, const char *prefix) {
    expect(!hit.is_miss(), prefix);
    expect(hit.inst_id == inst_id, "trace result has unexpected instance id");
    expect(hit.prim_id == prim_id, "trace result has unexpected primitive id");
    expect_near(hit.bary.x, bary.x, 1e-4f, "trace result has unexpected bary.x");
    expect_near(hit.bary.y, bary.y, 1e-4f, "trace result has unexpected bary.y");
}

void expect_miss(const TriangleHit &hit, const char *message) {
    expect(hit.is_miss(), message);
}

float3 sample_ray_origin(float translation_x) {
    return make_float3(translation_x + 0.2f, -0.1f, 5.f);
}

float3 sample_ray_direction() {
    return make_float3(0.f, 0.f, -1.f);
}

void expect_front_face_sample(Device &device, const Accel &accel, float translation_x) {
    TriangleHit hit = trace_hit(device, accel, sample_ray_origin(translation_x), sample_ray_direction());
    expect_hit(hit, 0u, 0u, make_float2(0.3f, 0.3f),
               "trace should hit the translated cube front-face sample");
}

void test_fast_trace_build(Device &device) {
    Stream stream = device.create_stream();
    RHIMesh mesh = create_cube_mesh(device, stream);
    Accel accel = device.create_accel(FAST_TRACE);
    accel.add_instance(ocarina::move(mesh), make_float4x4(1.f));
    stream << accel.build_bvh();
    stream << synchronize() << commit();
    expect(trace_center_hit(device, accel) == 1u,
           "FAST_TRACE accel should hit the cube after TLAS build");
    expect_front_face_sample(device, accel, 0.f);
}

void test_fast_trace_update_command_falls_back_to_build(Device &device) {
    Stream stream = device.create_stream();
    RHIMesh mesh = create_cube_mesh(device, stream);
    Accel accel = device.create_accel(FAST_TRACE);
    accel.add_instance(ocarina::move(mesh), make_float4x4(1.f));
    stream << accel.build_bvh();
    stream << synchronize() << commit();

    expect(trace_center_hit(device, accel) == 1u,
           "FAST_TRACE accel should hit before transform change");
    expect_front_face_sample(device, accel, 0.f);

    accel.set_transform(0u, transform::translation<H>(10.f, 0.f, 0.f));
    stream << accel.update_bvh();
    stream << synchronize() << commit();

    expect_miss(trace_hit(device, accel, sample_ray_origin(0.f), sample_ray_direction()),
                "FAST_TRACE accel should miss at the stale world-space sample after rebuild");
    expect_front_face_sample(device, accel, 10.f);
}

void test_fast_update_transform_refit(Device &device) {
    Stream stream = device.create_stream();
    RHIMesh mesh = create_cube_mesh(device, stream);
    Accel accel = device.create_accel(FAST_UPDATE);
    accel.add_instance(ocarina::move(mesh), make_float4x4(1.f));
    stream << accel.build_bvh();
    stream << synchronize() << commit();

    expect(trace_center_hit(device, accel) == 1u,
           "FAST_UPDATE accel should hit the cube before TLAS update");
    expect_front_face_sample(device, accel, 0.f);

    accel.set_transform(0u, transform::translation<H>(10.f, 0.f, 0.f));
    stream << accel.update_bvh();
    stream << synchronize() << commit();
    expect_miss(trace_hit(device, accel, sample_ray_origin(0.f), sample_ray_direction()),
                "FAST_UPDATE accel should miss at the stale world-space sample after refit");
    expect_front_face_sample(device, accel, 10.f);

    accel.set_transform(0u, make_float4x4(1.f));
    stream << accel.update_bvh();
    stream << synchronize() << commit();
    expect_miss(trace_hit(device, accel, sample_ray_origin(10.f), sample_ray_direction()),
                "FAST_UPDATE accel should miss at the translated sample after restoring the transform");
    expect_front_face_sample(device, accel, 0.f);
}

}// namespace

int main(int argc, char *argv[]) {
    RHIContext &context = RHIContext::instance();
    context.clear_cache();
    Device device = context.create_device("cuda");
    device.init_rtx();

    test_fast_trace_build(device);
    test_fast_trace_update_command_falls_back_to_build(device);
    test_fast_update_transform_refit(device);
    return 0;
}