#include "tests/backend_test.h"
#include "trace_mode_images.h"
#include "backends/vulkan_compute/vulkan_interop.h"
#include "tests/runtime/trace_scene_oracle.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <latch>
#include <numeric>
#include <optional>
#include <string_view>

using namespace ocarina;
using ocarina::test::require;

namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

ShaderExecutionMode expected_mode(TraceMode mode, TraceCapabilities caps) {
    if (mode == TraceMode::RayTracingPipeline || (mode == TraceMode::Auto && !caps.ray_query))
        return ShaderExecutionMode::RayTracingPipeline;
    return ShaderExecutionMode::ComputeRayQuery;
}

void check_mode(const auto &shader, TraceMode requested, ShaderExecutionMode expected) {
    require(shader.requested_options().trace_mode == requested, "requested trace mode changed");
    require(shader.execution_mode() == expected, "unexpected execution mode");
}

double mean_gpu_ms(TraceMode mode, uint repeats) {
    const auto *path = std::getenv("REBLUR_GPU_PROFILE");
    require(path, "performance case requires per-dispatch GPU timestamps");
    std::ifstream timestamps(path);
    const std::string name = mode == TraceMode::RayQuery ? "trace-local-query" : "trace-local-pipeline";
    std::vector<double> samples;
    for (std::string row; std::getline(timestamps, row);) {
        if (row.find(",\"" + name + "\",") == std::string::npos) continue;
        const auto sample = std::stod(row.substr(row.rfind(',') + 1u));
        require(std::isfinite(sample) && sample >= 0., "invalid GPU timestamp interval");
        samples.push_back(sample);
    }
    require(samples.size() == 1u + repeats, "missing timestamp rows for warmup or measured dispatches");
    const double average = std::accumulate(samples.begin() + 1, samples.end(), 0.) / repeats;
    require(average > 0., "GPU timestamp intervals all have zero duration");
    return average;
}

void plain_compute(Device &device) {
    Kernel kernel = [](BufferVar<uint> out) {
        Uint i = dispatch_id();
        out.write(i, i + 91u);
    };
    auto out = device.create_buffer<uint>(37u);
    auto stream = device.create_stream();
    array<uint, 37u> values{};
    for (auto request : {TraceMode::Auto, TraceMode::RayQuery, TraceMode::RayTracingPipeline}) {
        auto shader = device.compile(kernel, "plain-compute", CS, {request});
        check_mode(shader, request, ShaderExecutionMode::Compute);
        stream << out.byte_set(0xcdu) << shader(out).dispatch(37u) << out.download(values.data())
               << synchronize() << commit();
        for (uint i = 0; i < values.size(); ++i) require(values[i] == i + 91u, "plain compute dispatch failed");
    }
}

int transformed_scene(Device &device, std::string_view which) {
    const auto caps = device.trace_capabilities();
    const bool both = which == "concurrent" || which == "performance";
    if (((which == "query" || both) && !caps.ray_query) ||
        ((which == "pipeline" || both) && !caps.ray_tracing_pipeline) ||
        (!caps.ray_query && !caps.ray_tracing_pipeline)) return 77;
    device.init_rtx();
    test::Scene scene;
    const uint3 dim = make_uint3(16u, 11u, 2u);
    const auto rays = test::make_test_rays(dim);
    auto vertices = device.create_buffer<float3>(scene.vertices.size());
    auto triangles = device.create_buffer<Triangle>(scene.triangles.size());
    auto input = device.create_buffer<Ray>(rays.size());
    auto produced = device.create_buffer<Ray>(rays.size());
    auto hits = device.create_buffer<TriangleHit>(rays.size());
    auto distances = device.create_buffer<float>(rays.size());
    auto metadata = device.create_buffer<uint4>(2u * rays.size());
    auto consumed = device.create_buffer<uint4>(2u * rays.size());
    auto stream = device.create_stream();
    stream << vertices.upload(scene.vertices.data()) << triangles.upload(scene.triangles.data())
           << input.upload(rays.data()) << synchronize() << commit();
    auto accel = device.create_accel(FAST_UPDATE);
    auto build = [&] {
        for (const auto &transform : scene.transforms) {
            auto mesh = device.create_mesh(vertices.view(), triangles.view(), FAST_TRACE, DISABLE_ANYHIT);
            stream << mesh.build_bvh() << commit();
            accel.add_instance(ocarina::move(mesh), transform);
        }
        stream << accel.build_bvh() << synchronize() << commit();
    };
    build();

    Kernel kernel = [&](BufferVar<Ray> in, BufferVar<TriangleHit> out, BufferVar<uint4> meta,
                        BufferVar<float> hit_distances) {
        Uint i = dispatch_id();
        Var<Ray> ray = in.read(i);
        meta.write(2u * i, make_uint4(dispatch_idx(), i));
        outline([&] {
            if (which == "performance") {
                out.write(i, accel.trace_closest(ray));
            } else {
                Float distance = -1.f;
                out.write(i, accel.trace_closest(ray, distance));
                hit_distances.write(i, distance);
            }
            meta.write(2u * i + 1u, make_uint4(dispatch_dim(), cast<uint>(accel.trace_occlusion(ray))));
        }, "trace-local-traversal");
    };
    kernel.function()->configure(make_uint3(1u), make_uint3(4u, 3u, 2u));
    Kernel produce = [](BufferVar<Ray> in, BufferVar<Ray> out) { out.write(dispatch_id(), in.read(dispatch_id())); };
    Kernel consume = [](BufferVar<uint4> in, BufferVar<uint4> out) { out.write(dispatch_id(), in.read(dispatch_id())); };
    auto producer = device.compile(produce, "trace-input");
    auto consumer = device.compile(consume, "trace-output");

    vector<TriangleHit> read_hits(rays.size());
    vector<float> read_distances(rays.size());
    vector<uint4> read_meta(2u * rays.size());
    auto run = [&](const auto &shader, TraceMode mode) {
        check_mode(shader, mode, expected_mode(mode, caps));
        const auto invocation = shader(produced, hits, metadata, distances);
        auto render = [&] {
            stream << hits.byte_set(0xcdu) << metadata.byte_set(0xcdu) << consumed.byte_set(0xcdu)
                   << producer(input, produced).dispatch(uint(rays.size()))
                   << invocation.dispatch(dim)
                   << consumer(metadata, consumed).dispatch(uint(read_meta.size()))
                   << hits.download(read_hits.data()) << consumed.download(read_meta.data());
            if (which != "performance") stream << distances.download(read_distances.data());
            stream << synchronize() << commit();
        };
        auto verify = [&] {
            render();
            test::validate_scene(scene, dim, rays, read_hits, read_meta);
            if (which != "performance") for (uint i = 0u; i < rays.size(); ++i) {
                float expected_distance;
                (void)test::trace_reference(scene, rays[i], &expected_distance);
                require(std::abs(read_distances[i] - expected_distance) <= 3e-4f,
                        "closest hit distance differs from transformed host triangles");
            }
        };
        verify();
        if (which == "query" || which == "pipeline") {
            scene.transforms[1][3].x -= 1.4f;
            scene.transforms[1][3].z += .6f;
            scene.transforms[2][0].x = -.6f;
            for (uint i = 0; i < scene.transforms.size(); ++i) accel.set_transform(i, scene.transforms[i]);
            stream << accel.update_bvh() << synchronize() << commit();
            require(accel.last_build_action() == Accel::BuildAction::UPDATE, "expected transform refit");
            verify();
            accel.clear();
            render();
            for (uint i = 0; i < rays.size(); ++i)
                require(test::has_miss_payload(read_hits[i]) && read_meta[2u * i + 1u].w == 0u &&
                        read_distances[i] == 0.f,
                        "reused invocation returned an incorrect miss payload or occlusion after clear");
            build();
            verify();
        }
        if (which != "performance" && shader.execution_mode() == ShaderExecutionMode::RayTracingPipeline) {
            stream << metadata.byte_set(0xcdu)
                   << invocation.dispatch(0u, dim.y, dim.z) << invocation.dispatch(dim.x, 0u, dim.z)
                   << invocation.dispatch(dim.x, dim.y, 0u)
                   << metadata.download(read_meta.data()) << synchronize() << commit();
            for (const auto &value : read_meta)
                require(all(value == make_uint4(0xcdcdcdcdu)), "zero-sized native dispatch wrote an output");
        }
        if (which == "performance") {
            constexpr uint repeats = 16u;
            auto start = Clock::now();
            for (uint i = 0; i < repeats; ++i) stream << invocation.dispatch(dim);
            stream << hits.download(read_hits.data()) << metadata.download(read_meta.data()) << synchronize() << commit();
            double end_to_end = elapsed_ms(start);
            test::validate_scene(scene, dim, rays, read_hits, read_meta);
            std::cout << "mode=" << (mode == TraceMode::RayQuery ? "query" : "pipeline")
                      << " rays_per_dispatch=" << rays.size() << " repeats=" << repeats
                      << " gpu_mean_dispatch_ms=" << mean_gpu_ms(mode, repeats)
                      << " batch_submit_to_readback_ms=" << end_to_end << std::endl;
        }
    };
    auto compile = [&](TraceMode mode) {
        const char *name = mode == TraceMode::RayQuery ? "trace-local-query" : "trace-local-pipeline";
        auto start = Clock::now();
        auto shader = device.compile(kernel, name, CS, {mode});
        if (which == "performance") std::cout << name << " create_ms=" << elapsed_ms(start) << std::endl;
        return shader;
    };
    if (which == "concurrent") {
        std::latch ready(2);
        auto compile_together = [&](TraceMode mode) {
            ready.arrive_and_wait();
            return compile(mode);
        };
        auto query = std::async(std::launch::async, compile_together, TraceMode::RayQuery);
        auto pipeline = std::async(std::launch::async, compile_together, TraceMode::RayTracingPipeline);
        auto q = query.get();
        auto p = pipeline.get();
        run(q, TraceMode::RayQuery);
        run(p, TraceMode::RayTracingPipeline);
        run(q, TraceMode::RayQuery);
    } else if (which == "performance") {
        auto q = compile(TraceMode::RayQuery);
        auto p = compile(TraceMode::RayTracingPipeline);
        run(q, TraceMode::RayQuery);
        run(p, TraceMode::RayTracingPipeline);
    } else if (which == "auto") {
        auto future = device.async_compile(ocarina::move(kernel), "trace-local-auto");
        run(future.get(), TraceMode::Auto);
    } else {
        const auto mode = which == "query" ? TraceMode::RayQuery : TraceMode::RayTracingPipeline;
        const char *name = which == "query" ? "trace-local-query" : "trace-local-pipeline";
        auto future = device.async_compile(ocarina::move(kernel), name, CS, {mode});
        run(future.get(), mode);
    }
    return 0;
}

std::optional<uint3> oversized_dispatch(Stream &stream, bool axis) {
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &rt};
    stream << native_callback([&](const NativeInteropContext &context) {
        const auto &vk = *static_cast<const VulkanInteropContext *>(context.native);
        auto get = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            vk.get_instance_proc_addr(vk.instance, "vkGetPhysicalDeviceProperties2"));
        require(get, "missing Vulkan property query");
        get(vk.physical_device, &props);
    }) << synchronize() << commit();
    const auto &limits = props.properties.limits;
    uint64_t remaining = uint64_t(rt.maxRayDispatchInvocationCount) + 1u;
    array<uint, 3> dims{1u, 1u, 1u};
    for (uint i = 0; i < 3u; ++i) {
        const auto max_axis = uint64_t(limits.maxComputeWorkGroupCount[i]) * limits.maxComputeWorkGroupSize[i];
        if (axis) {
            // Isolate the axis check. Some devices cannot exceed an axis without first
            // exceeding the total count, or cannot express an illegal axis through the RHI.
            if (max_axis >= UINT32_MAX || max_axis >= rt.maxRayDispatchInvocationCount) continue;
            dims[i] = uint(max_axis + 1u);
            return make_uint3(dims[0], dims[1], dims[2]);
        }
        dims[i] = uint(std::min({remaining, max_axis, uint64_t(UINT32_MAX)}));
        require(dims[i] > 0u, "unexpected zero Vulkan dimension limit");
        remaining = (remaining + dims[i] - 1u) / dims[i];
    }
    if (axis || remaining > 1u) return {};
    return make_uint3(dims[0], dims[1], dims[2]);
}

int reject(Device &device, std::string_view reason) {
    const auto caps = device.trace_capabilities();
    TraceMode request = TraceMode::RayTracingPipeline;
    if (reason == "unavailable-query") {
        if (caps.ray_query) return 77;
        request = TraceMode::RayQuery;
    }
    if (reason == "unavailable-pipeline" && caps.ray_tracing_pipeline) return 77;
    const bool limit = reason == "limit-axis" || reason == "limit-product";
    if (limit && !caps.ray_tracing_pipeline) return 77;
    auto restriction = [&] {
        if (reason == "workgroup") { synchronize_block(); }
        if (reason == "warp") return Uint(warp_lane_id());
        if (reason == "unknown") include("trace-mode-unknown.slang");
        return Uint(0u);
    };
    Kernel kernel = [&](Var<Accel> as, BufferVar<uint> out) {
        Float3 origin = make_float3(0.f);
        auto ray = make_ray(origin, make_float3(0.f, 0.f, 1.f));
        out.write(0u, cast<uint>(as.trace_occlusion(ray)) + outline(restriction, "trace-local-restriction"));
    };
    std::cout << "attempting rejection " << reason << std::endl;
    auto shader = device.compile(kernel, "trace-local-rejection", CS, {request});
    if (limit) {
        auto as = device.create_accel();
        auto out = device.create_buffer<uint>(1u);
        auto stream = device.create_stream();
        const auto dim = oversized_dispatch(stream, reason == "limit-axis");
        if (!dim) {
            std::cout << "no independent violating dispatch is representable on this device" << std::endl;
            return 77;
        }
        stream << shader(as, out).dispatch(*dim) << synchronize() << commit();
    }
    return 0;
}
}// namespace

int main(int argc, char **argv) {
    require(argc >= 2, "expected a trace-mode case");
    const auto which = std::string_view(argv[1]);
    if (argc > 2) test::RHIContextTestAccess::set_cache_directory(argv[2]);
    auto device = create_test_device();
    const auto caps = device.trace_capabilities();
    std::cout << "trace capabilities ray_query=" << caps.ray_query << " ray_tracing_pipeline="
              << caps.ray_tracing_pipeline << std::endl;
    if (which == "query-images") return test::trace_mode_images(device, TraceMode::RayQuery);
    if (which == "pipeline-images") return test::trace_mode_images(device, TraceMode::RayTracingPipeline);
    if (which == "compute") { plain_compute(device); return 0; }
    if (which.starts_with("reject-")) return reject(device, which.substr(7));
    require(which == "query" || which == "pipeline" || which == "auto" ||
            which == "concurrent" || which == "performance", "unknown trace-mode case");
    return transformed_scene(device, which);
}
