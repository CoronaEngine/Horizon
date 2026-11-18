#pragma once

#include "dsl/dsl.h"
#include "math/geometry.h"
#include "rhi/common.h"
#include <stdexcept>

namespace ocarina::test {

inline int trace_mode_images(Device &device, TraceMode mode) {
    const auto caps = device.trace_capabilities();
    if (mode == TraceMode::RayQuery ? !caps.ray_query : !caps.ray_tracing_pipeline) return 77;
    auto stream = device.create_stream();
    auto empty = device.create_accel(FAST_UPDATE);
    const auto extent = make_uint3(2u, 2u, 2u);
    auto plane = device.create_texture2d(make_uint2(4u, 2u), PixelStorage::UINT4);
    auto volume = device.create_texture3d(extent, PixelStorage::UINT4);
    auto plane_copy = device.create_texture2d(make_uint2(4u, 2u), PixelStorage::UINT4);
    auto volume_copy = device.create_texture3d(extent, PixelStorage::UINT4);
    // This 128-byte argument exercises the batch parameter arena in both shader stages.
    Kernel kernel = [&](Texture2DVar a, Texture3DVar b, Var<array<uint, 32>> offsets) {
        Uint i = dispatch_id();
        Uint3 xyz = dispatch_idx();
        Uint2 xy = make_uint2(i % 4u, i / 4u);
        Float origin_x = 0.f;
        auto ray = make_ray(make_float3(origin_x, 0.f, 0.f), make_float3(0.f, 0.f, 1.f), 3.f);
        Uint increment = offsets[i] + cast<uint>(empty.trace_occlusion(ray));
        a.write(a.read<uint4>(xy) + increment, xy);
        b.write(b.read<uint4>(xyz) + 2u * increment, xyz);
    };
    auto shader = device.compile(kernel, "trace-mode-image-transfer", CS, {mode});
    const auto expected_mode = mode == TraceMode::RayQuery ? ShaderExecutionMode::ComputeRayQuery :
                                                              ShaderExecutionMode::RayTracingPipeline;
    if (shader.execution_mode() != expected_mode) throw std::runtime_error("Image dispatch resolved to the wrong trace mode");
    array<uint, 32> offsets{};
    array<uint4, 8> seed{}, result2d{}, result3d{};
    for (uint i = 0; i < seed.size(); ++i) {
        offsets[i] = 3u * i + 1u;
        seed[i] = make_uint4(11u, 17u, 23u, 29u) + i;
    }
    stream << plane.upload(seed.data()) << volume.upload(seed.data())
           << shader(plane, volume, offsets).dispatch(extent)
           << plane_copy.copy_from(plane) << volume_copy.copy_from(volume)
           << plane_copy.download(result2d.data()) << volume_copy.download(result3d.data())
           << synchronize() << commit();
    for (uint i = 0; i < seed.size(); ++i) {
        if (!all(result2d[i] == seed[i] + offsets[i])) throw std::runtime_error("2D trace image transfer differs");
        if (!all(result3d[i] == seed[i] + 2u * offsets[i])) throw std::runtime_error("3D trace image transfer differs");
    }
    return 0;
}

}// namespace ocarina::test
