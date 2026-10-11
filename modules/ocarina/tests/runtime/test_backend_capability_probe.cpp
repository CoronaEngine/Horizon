#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "math/transform.h"
#include "rhi/context.h"

#include <cmath>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>

using namespace ocarina;

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "CAPABILITY_PROBE result=FAIL reason=" << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void report_action(const char *phase, const Accel &accel) {
    std::cout << "CAPABILITY_ACTION phase=" << phase
              << " action=" << uint(accel.last_build_action())
              << " builds=" << accel.build_count() << " updates=" << accel.update_count()
              << " instances=" << accel.mesh_num() << " handle=" << accel.handle() << std::endl;
}

void check_ray(Device &device, Stream &stream, const Accel &accel,
               float x, float t_max, bool expected_hit, const char *phase) {
    auto hits = device.create_buffer<TriangleHit>(1u);
    auto visibility = device.create_buffer<uint>(1u);
    Kernel kernel = [&accel](BufferVar<TriangleHit> hits, BufferVar<uint> visibility,
                            Float origin_x, Float maximum) {
        auto ray = make_ray(make_float3(origin_x, 0.f, 0.f), make_float3(0.f, 0.f, 1.f), maximum);
        hits.write(0u, accel.trace_closest(ray));
        visibility.write(0u, cast<uint>(accel.trace_occlusion(ray)));
    };
    auto shader = device.compile(kernel, "backend-capability-ray");
    TriangleHit hit{};
    uint visible = 0xcdcdcdcdu;
    // Reset GPU outputs too: a miss/error must not accidentally reuse an old hit.
    stream << hits.byte_set(0xcdu) << visibility.byte_set(0xcdu)
           << shader(hits, visibility, x, t_max).dispatch(1u)
           << hits.download(&hit) << visibility.download(&visible) << synchronize() << commit();
    std::cout << "CAPABILITY_RAY phase=" << phase << " origin_x=" << x << " tmax=" << t_max
              << " inst=" << hit.inst_id << " prim=" << hit.prim_id
              << " bary=" << hit.bary.x << ',' << hit.bary.y
              << " occluded=" << visible << std::endl;
    require(visible == uint(expected_hit), "trace_occlusion disagrees with the analytic triangle oracle");
    require(hit.is_miss() != expected_hit, "trace_closest disagrees with the analytic triangle oracle");
    if (expected_hit) {
        require(hit.inst_id == 0u && hit.prim_id == 0u, "hit instance or primitive index differs");
        // Native (u, v) is (.25, .5); Ocarina stores (b0, b1) = (1-u-v, u).
        require(std::isfinite(hit.bary.x) && std::isfinite(hit.bary.y) &&
                    std::abs(hit.bary.x - .25f) <= 1e-5f && std::abs(hit.bary.y - .25f) <= 1e-5f,
                "hit barycentrics differ from the analytic triangle oracle");
    }
}

array<float3, 3> triangle_at(float z) {
    return {make_float3(-1.f, -1.f, z), make_float3(1.f, -1.f, z), make_float3(0.f, 1.f, z)};
}

void probe_empty_accel(Device &device) {
    auto stream = device.create_stream();
    auto accel = device.create_accel(FAST_UPDATE);
    stream << accel.build_bvh() << synchronize() << commit();
    require(accel.mesh_num() == 0u && accel.handle() == 0u && accel.build_count() == 0u &&
                accel.update_count() == 0u && accel.last_build_action() == Accel::BuildAction::NONE,
            "building an empty scene did not preserve the empty acceleration state");
    report_action("empty-build", accel);
    stream << accel.update_bvh() << synchronize() << commit();
    require(accel.mesh_num() == 0u && accel.handle() == 0u && accel.build_count() == 0u &&
                accel.update_count() == 0u && accel.last_build_action() == Accel::BuildAction::NONE,
            "updating an empty scene did not preserve the empty acceleration state");
    report_action("empty-update", accel);
    check_ray(device, stream, accel, 0.f, 3.f, false, "empty-trace");

    auto vertices = device.create_buffer<float3>(3u);
    auto indices = device.create_buffer<Triangle>(1u);
    auto points = triangle_at(2.f);
    Triangle triangle(0u, 1u, 2u);
    auto mesh = device.create_mesh(vertices.view(), indices.view());
    auto *build_mesh = mesh.build_bvh();
    accel.add_instance(ocarina::move(mesh), make_float4x4(1.f));
    stream << vertices.upload(points.data()) << indices.upload(&triangle)
           << build_mesh << accel.build_bvh() << synchronize() << commit();
    auto output = device.create_buffer<uint4>(2u);
    Kernel kernel = [&accel](Var<Accel> explicit_accel, BufferVar<uint4> out) {
        Float origin_x = 0.f;
        auto ray = make_ray(make_float3(origin_x, 0.f, 0.f), make_float3(0.f, 0.f, 1.f), 3.f);
        auto captured_hit = accel.trace_closest(ray);
        auto explicit_hit = explicit_accel.trace_closest(ray);
        out.write(0u, make_uint4(captured_hit.inst_id, cast<uint>(accel.trace_occlusion(ray)), 0u, 73u));
        out.write(1u, make_uint4(explicit_hit.inst_id, cast<uint>(explicit_accel.trace_occlusion(ray)), 0u, 79u));
    };
    auto shader = device.compile(kernel, "capability-cleared-accel-invocation");
    auto *before_clear = shader(accel, output).dispatch(1u);
    accel.clear();
    array<uint4, 2> result{};
    stream << output.byte_set(0xcdu) << before_clear << output.download(result.data()) << synchronize() << commit();
    require(result[0].x == ~0u && result[0].y == 0u && result[0].w == 73u &&
                result[1].x == ~0u && result[1].y == 0u && result[1].w == 79u,
            "cleared AS invocation retained an invalid handle or failed to return miss");
}

void probe_fast_build(Device &device) {
    auto stream = device.create_stream();
    auto vertices = device.create_buffer<float3>(3u);
    auto indices = device.create_buffer<Triangle>(1u);
    auto points = triangle_at(2.f);
    Triangle triangle(0u, 1u, 2u);
    stream << vertices.upload(points.data()) << indices.upload(&triangle) << synchronize() << commit();
    auto mesh = device.create_mesh(vertices.view(), indices.view(), FAST_BUILD, NONE);
    stream << mesh.build_bvh() << synchronize() << commit();
    auto accel = device.create_accel(FAST_BUILD);
    accel.add_instance(ocarina::move(mesh), make_float4x4(1.f));
    stream << accel.build_bvh() << synchronize() << commit();
    report_action("fast-build", accel);
    require(accel.last_build_action() == Accel::BuildAction::BUILD && accel.build_count() == 1u,
            "FAST_BUILD did not report a completed initial build");
    check_ray(device, stream, accel, 0.f, 3.f, true, "fast-build");
    accel.set_transform(0u, transform::translation<H>(4.f, 0.f, 0.f));
    stream << accel.update_bvh() << synchronize() << commit();
    report_action("fast-build-update", accel);
    require(accel.last_build_action() == Accel::BuildAction::BUILD && accel.build_count() == 2u &&
                accel.update_count() == 0u,
            "FAST_BUILD update must fall back to a full build");
    check_ray(device, stream, accel, 0.f, 3.f, false, "fast-build-old-position");
    check_ray(device, stream, accel, 4.f, 3.f, true, "fast-build-new-position");
}

void probe_mesh_rebuild(Device &device) {
    auto stream = device.create_stream();
    auto vertices = device.create_buffer<float3>(3u);
    auto indices = device.create_buffer<Triangle>(1u);
    auto points = triangle_at(2.f);
    Triangle triangle(0u, 1u, 2u);
    stream << vertices.upload(points.data()) << indices.upload(&triangle) << synchronize() << commit();
    auto mesh = device.create_mesh(vertices.view(), indices.view(), FAST_UPDATE,
                                  AccelGeomTag(SINGLE_ANYHIT_CALL | DISABLE_FACE_CULLING));
    stream << mesh.build_bvh() << synchronize() << commit();
    auto *mesh_impl = mesh.impl();
    auto previous_blas = mesh_impl->blas_handle();
    // Prepare the command while this public mesh owns the handle. The Accel
    // then retains ownership throughout its later execution and readback.
    auto *rebuild = mesh.build_bvh();
    auto accel = device.create_accel(FAST_UPDATE);
    accel.add_instance(ocarina::move(mesh), make_float4x4(1.f));
    stream << accel.build_bvh() << synchronize() << commit();
    check_ray(device, stream, accel, 0.f, 3.f, true, "before-mesh-rebuild");
    points = triangle_at(4.f);
    stream << vertices.upload(points.data()) << rebuild << synchronize() << commit();
    auto next_blas = mesh_impl->blas_handle();
    std::cout << "CAPABILITY_BLAS previous=" << previous_blas << " next=" << next_blas
              << " handle_changed=" << uint(previous_blas != next_blas) << std::endl;
    stream << accel.update_bvh() << synchronize() << commit();
    report_action("after-mesh-rebuild-update", accel);
    // BuildAction is recorded, not prescribed: a backend may rebuild rather
    // than refit when a referenced BLAS address changes. Visibility must agree.
    check_ray(device, stream, accel, 0.f, 3.f, false, "mesh-rebuild-near");
    check_ray(device, stream, accel, 0.f, 5.f, true, "mesh-rebuild-far");
}

void probe_same_batch_accel(Device &device) {
    auto stream = device.create_stream();
    auto vertices = device.create_buffer<float3>(3u);
    auto indices = device.create_buffer<Triangle>(1u);
    auto output = device.create_buffer<uint4>(4u);
    auto points = triangle_at(2.f);
    Triangle triangle(0u, 1u, 2u);
    auto mesh = device.create_mesh(vertices.view(), indices.view(), FAST_TRACE, DISABLE_ANYHIT);
    auto *build_mesh = mesh.build_bvh();
    auto accel = device.create_accel(FAST_TRACE);
    accel.add_instance(ocarina::move(mesh), make_float4x4(1.f));
    Kernel captured = [&accel](BufferVar<uint4> out, Uint slot, Uint token, Float x) {
        auto ray = make_ray(make_float3(x, 0.f, 0.f), make_float3(0.f, 0.f, 1.f), 3.f);
        auto hit = accel.trace_closest(ray);
        out.write(slot, make_uint4(token, cast<uint>(accel.trace_occlusion(ray)), hit.inst_id, hit.prim_id));
    };
    Kernel explicit_arg = [](Var<Accel> scene, BufferVar<uint4> out, Uint slot, Uint token, Float x) {
        auto ray = make_ray(make_float3(x, 0.f, 0.f), make_float3(0.f, 0.f, 1.f), 3.f);
        auto hit = scene.trace_closest(ray);
        out.write(slot, make_uint4(token, cast<uint>(scene.trace_occlusion(ray)), hit.inst_id, hit.prim_id));
    };
    auto captured_shader = device.compile(captured, "capability-late-captured-accel");
    auto explicit_shader = device.compile(explicit_arg, "capability-late-explicit-accel");
    uint token = 17u;
    float x = 0.f;
    // Construct both invocations before the first BLAS/TLAS exists. POD values
    // must still be frozen here even though the AS handle resolves at dispatch.
    auto *first = captured_shader(output, 0u, token, x).dispatch(1u);
    auto *second = explicit_shader(accel, output, 1u, token, x).dispatch(1u);
    token = 999u;
    x = 100.f;
    array<uint4, 4> result{};
    stream << output.byte_set(0xcdu) << vertices.upload(points.data()) << indices.upload(&triangle)
           << build_mesh << accel.build_bvh() << first << second
           << output.download(result.data()) << synchronize() << commit();
    for (uint i = 0; i < 2u; ++i) {
        require(result[i].x == 17u && result[i].y == 1u && result[i].z == 0u && result[i].w == 0u,
                "same-batch upload/build/trace lost an AS handle or ordinary argument snapshot");
    }

    auto *old_captured = captured_shader(output, 0u, 23u, 0.f).dispatch(1u);
    auto *old_explicit = explicit_shader(accel, output, 1u, 29u, 0.f).dispatch(1u);
    auto *new_captured = captured_shader(output, 2u, 31u, 4.f).dispatch(1u);
    auto *new_explicit = explicit_shader(accel, output, 3u, 37u, 4.f).dispatch(1u);
    // FAST_TRACE update rebuilds and may compact to a different native handle.
    // All four commands above must use the result of this preceding update.
    accel.set_transform(0u, transform::translation<H>(4.f, 0.f, 0.f));
    stream << output.byte_set(0xcdu) << accel.update_bvh()
           << old_captured << old_explicit << new_captured << new_explicit
           << output.download(result.data()) << synchronize() << commit();
    constexpr uint tokens[]{23u, 29u, 31u, 37u};
    for (uint i = 0; i < 4u; ++i) {
        require(result[i].x == tokens[i] && result[i].y == uint(i >= 2u) &&
                    result[i].z == (i >= 2u ? 0u : ~0u),
                "prebuilt invocation used stale AS after a same-batch rebuild");
        if (i >= 2u) require(result[i].w == 0u, "rebuilt AS returned an incorrect primitive");
    }
}

void probe_multistream_ray_params(Device &device) {
    constexpr uint count = 16384u, rounds = 8u, queries = 32u;
    auto first_stream = device.create_stream();
    auto second_stream = device.create_stream();
    auto vertices = device.create_buffer<float3>(3u);
    auto indices = device.create_buffer<Triangle>(1u);
    auto points = triangle_at(2.f);
    Triangle triangle(0u, 1u, 2u);
    auto mesh = device.create_mesh(vertices.view(), indices.view());
    auto *build_mesh = mesh.build_bvh();
    auto accel = device.create_accel(FAST_TRACE);
    accel.add_instance(ocarina::move(mesh), make_float4x4(1.f));
    first_stream << vertices.upload(points.data()) << indices.upload(&triangle)
                 << build_mesh << accel.build_bvh() << synchronize() << commit();
    auto first_output = device.create_buffer<uint4>(count * rounds);
    auto second_output = device.create_buffer<uint4>(count * rounds);
    Kernel kernel = [&accel, queries](BufferVar<uint4> out, Uint token) {
        Uint i = dispatch_id();
        Uint hit_count = 0u;
        $for(step, queries) {
            Float x = cast<float>((step + i + token) & 1u) * 4.f;
            auto ray = make_ray(make_float3(x, 0.f, 0.f), make_float3(0.f, 0.f, 1.f), 3.f);
            TriangleHitVar hit = accel.trace_closest(ray);
            hit_count += cast<uint>(!hit->is_miss());
        };
        out.write(i, make_uint4(token, i, hit_count, token ^ i));
    };
    auto shader = device.compile(kernel, "capability-multistream-ray-params");
    // Warm both per-stream parameter allocations before interleaved work.
    first_stream << shader(first_output.view(0u, count), 1u).dispatch(count) << synchronize() << commit();
    second_stream << shader(second_output.view(0u, count), 2u).dispatch(count) << synchronize() << commit();
    first_stream << first_output.byte_set(0xcdu) << commit();
    second_stream << second_output.byte_set(0xcdu) << commit();
    for (uint round = 0; round < rounds; ++round) {
        first_stream << shader(first_output.view(round * count, count), 101u + round).dispatch(count) << commit();
        second_stream << shader(second_output.view(round * count, count), 1001u + round).dispatch(count) << commit();
    }
    // Destruction must retain every stream's pipeline/parameters until their
    // queued launches finish, even before either explicit synchronization.
    shader.destroy();
    vector<uint4> first(count * rounds), second(count * rounds);
    first_stream << first_output.download(first.data()) << synchronize() << commit();
    second_stream << second_output.download(second.data()) << synchronize() << commit();
    for (uint round = 0; round < rounds; ++round) {
        for (uint i = 0; i < count; ++i) {
            const auto a = first[round * count + i], b = second[round * count + i];
            const uint ta = 101u + round, tb = 1001u + round;
            require(a.x == ta && a.y == i && a.z == queries / 2u && a.w == (ta ^ i) &&
                        b.x == tb && b.y == i && b.z == queries / 2u && b.w == (tb ^ i),
                    "same ray shader cross-stream launch parameters or output views raced");
        }
    }

    // Reusing a single invocation across two submitting host threads must not
    // patch shared AS-argument bytes. The scene and all public resources remain
    // alive until both threads join and both streams finish. Only atomic writes
    // overlap between streams, so this test introduces no GPU data race.
    constexpr uint shared_count = 257u, shared_rounds = 16u, frozen_stamp = 3u;
    auto counts = device.create_buffer<uint>(shared_count);
    Kernel shared_kernel = [&accel](Var<Accel> explicit_accel, BufferVar<uint> counts, Uint stamp) {
        Uint i = dispatch_id();
        Float x = cast<float>(i & 1u) * 4.f;
        auto ray = make_ray(make_float3(x, 0.f, 0.f), make_float3(0.f, 0.f, 1.f), 3.f);
        TriangleHitVar explicit_hit = explicit_accel.trace_closest(ray);
        TriangleHitVar captured_hit = accel.trace_closest(ray);
        Uint increment = stamp + cast<uint>(!explicit_hit->is_miss()) + cast<uint>(!captured_hit->is_miss());
        counts.atomic(i).fetch_add(increment);
    };
    auto shared_shader = device.compile(shared_kernel, "capability-shared-ray-invocation");
    // Warm the backend's stream-local pipeline/parameter caches independently
    // of the concurrent phase, then reset every counter before the oracle run.
    first_stream << counts.byte_set(0u) << shared_shader(accel, counts, frozen_stamp).dispatch(shared_count)
                 << synchronize() << commit();
    second_stream << shared_shader(accel, counts, frozen_stamp).dispatch(shared_count)
                  << synchronize() << commit();
    first_stream << counts.byte_set(0u) << synchronize() << commit();
    uint stamp = frozen_stamp;
    const auto invocation = shared_shader(accel, counts, stamp);
    stamp = 99u;
    vector<ShaderDispatchCommand *> first_commands, second_commands;
    for (uint round = 0u; round < shared_rounds; ++round) {
        first_commands.push_back(invocation.dispatch(shared_count));
        second_commands.push_back(invocation.dispatch(shared_count - 16u));
    }
    std::atomic<uint> ready{0u};
    std::atomic<bool> start{false};
    auto submit = [&](Stream &stream, const vector<ShaderDispatchCommand *> &commands) {
        ready.fetch_add(1u, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (auto *command : commands) stream << command << commit();
    };
    std::thread first_submit([&] { submit(first_stream, first_commands); });
    std::thread second_submit([&] { submit(second_stream, second_commands); });
    while (ready.load(std::memory_order_acquire) != 2u) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    first_submit.join();
    second_submit.join();
    first_stream << synchronize() << commit();
    second_stream << synchronize() << commit();
    vector<uint> observed(shared_count, 0xcdcdcdcdu);
    first_stream << counts.download(observed.data()) << synchronize() << commit();
    for (uint i = 0u; i < shared_count; ++i) {
        const uint visits = shared_rounds * (i < shared_count - 16u ? 2u : 1u);
        const uint increment = frozen_stamp + (i % 2u == 0u ? 2u : 0u);
        require(observed[i] == visits * increment,
                "shared ray invocation raced AS patches or lost its POD/dimension snapshot");
    }
}
}

int main(int argc, char **argv) {
    require(argc == 3 && std::string_view(argv[1]) == "--case",
            "usage: test-backend-capability-probe --case empty-accel|fast-build|mesh-rebuild|same-batch-accel|multistream-ray-params");
    const std::string_view name(argv[2]);
    require(name == "empty-accel" || name == "fast-build" || name == "mesh-rebuild" ||
                name == "same-batch-accel" || name == "multistream-ray-params", "unknown case");
    const char *backend = test_backend_name();
    std::cout << "CAPABILITY_PROBE backend=" << backend << " case=" << name << " phase=attempt" << std::endl;
    auto device = RHIContext::instance().create_device(backend);
    device.init_rtx();
    if (name == "empty-accel") probe_empty_accel(device);
    else if (name == "fast-build") probe_fast_build(device);
    else if (name == "mesh-rebuild") probe_mesh_rebuild(device);
    else if (name == "same-batch-accel") probe_same_batch_accel(device);
    else probe_multistream_ray_params(device);
    std::cout << "CAPABILITY_PROBE backend=" << backend << " case=" << name << " result=PASS" << std::endl;
    return EXIT_SUCCESS;
}
