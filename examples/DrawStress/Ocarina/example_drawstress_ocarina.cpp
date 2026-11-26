#include "DrawStressOcarina.h"

#include "dsl/dsl.h"
#include "math/ktm_bridge.h"
#include "rhi/context.h"

#include <ktm/ktm.h>
#include <array>
#include <cstdint>

DrawStressOcarina::DrawStressOcarina(void* native_window)
    : ExampleBase(native_window), display(native_window, PresentDevicePreference::RenderDevice),
      device(ocarina::RHIContext::instance().create_device()), stream(device.create_stream())
{
    device.init_rtx();

    using ocarina::Float3, ocarina::make_float3;
    using ocarina::cast, ocarina::commit, ocarina::synchronize;

    const std::array<ocarina::uint3, 12> triangles = {{
        {0, 1, 2}, {1, 3, 2},
        {4, 6, 5}, {5, 6, 7},
        {0, 2, 4}, {4, 2, 6},
        {1, 5, 3}, {5, 7, 3},
        {0, 4, 1}, {4, 5, 1},
        {2, 3, 6}, {6, 3, 7},
    }};
    std::array<ocarina::float3, 8> corners;
    for (uint32_t i = 0; i < corners.size(); ++i)
        corners[i] = make_float3((i & 1) ? 1.f : -1.f, (i & 2) ? -1.f : 1.f, (i & 4) ? -1.f : 1.f);
    vertices = device.create_buffer<ocarina::float3>(corners.size());
    indices = device.create_buffer<ocarina::uint3>(triangles.size());
    stream << vertices.upload(corners.data()) << indices.upload(triangles.data()) << synchronize() << commit();

    auto cube = device.create_mesh(vertices, indices);
    stream << cube.build_bvh() << synchronize() << commit();
    grid = device.create_accel(ocarina::FAST_UPDATE);
    grid.add_instance(ocarina::move(cube), ocarina::make_float4x4(1.f));
    for (int i = 1; i < dim * dim * dim; ++i)
        grid.add_instance(0u, ocarina::make_float4x4(1.f));

    ocarina::Kernel trace = [&](Float3 eye, ocarina::BufferVar<uint32_t> output) {
        ocarina::Uint2 xy = ocarina::dispatch_idx().xy();
        ocarina::Uint2 extent = ocarina::dispatch_dim().xy();
        auto pixel = xy.y * extent.x + xy.x;
        ocarina::Float screen_x = (cast<float>(xy.x) + .5f) / ocarina::cast<float>(extent.x) * 2.f - 1.f;
        ocarina::Float screen_y = 1.f - (cast<float>(xy.y) + .5f) / ocarina::cast<float>(extent.y) * 2.f;
        ocarina::Float aspect = ocarina::cast<float>(extent.x) / ocarina::cast<float>(extent.y);
        constexpr float half_fov = .57735027f; // tan(60 degrees / 2)
        auto ray = ocarina::make_ray(eye, ocarina::normalize(
            make_float3(half_fov * (screen_x * aspect), half_fov * screen_y, 1.f)));
        auto hit = grid.trace_closest(ray);
        Float3 color = make_float3(.19f);
        $if (hit->is_hit()) {
            ocarina::Uint3 tri = indices.read(hit->prim_id);
            Float3 local = hit->triangle_lerp(vertices.read(tri.x), vertices.read(tri.y), vertices.read(tri.z));
            color = local * make_float3(.5f, -.5f, -.5f) + .5f;
        };
        Float3 bytes = ocarina::clamp(color, 0.f, 1.f) * 255.f + .5f;
        output.write(pixel, cast<uint32_t>(bytes.x) | (cast<uint32_t>(bytes.y) << 8u) |
                            (cast<uint32_t>(bytes.z) << 16u) | 0xff000000u);
    };
    shader = device.compile(trace, "drawstress_ocarina");
    pixels.resize(width * height);
    output_buffer = device.create_buffer<uint32_t>(pixels.size());
}

void DrawStressOcarina::run(float seconds)
{
    const auto scale = ktm::scale3d(ktm::fvec3(0.25f));
    for (int z = 0; z < dim; ++z) {
        const auto rotate_z = ktm::rotate3d_z(seconds + z * .13f);
        for (int y = 0; y < dim; ++y) {
            const auto rotate_y = ktm::rotate3d_y(seconds + y * .37f);
            for (int x = 0; x < dim; ++x) {
                const uint32_t instance = (z * dim + y) * dim + x;
                const ktm::fvec3 centre{.6f * (x - .5f * dim), .6f * (y - .5f * dim), -15.f + .6f * z};
                const auto transform = ktm::translate3d(centre) * rotate_z * rotate_y
                                     * ktm::rotate3d_x(seconds + x * .21f) * scale;
                grid.set_transform(instance, ocarina::bridge::from_ktm(transform));
            }
        }
    }
    stream << grid.update_bvh();
    const auto eye = ocarina::make_float3(0.f, 0.f, -35.f);
    stream << shader(eye, output_buffer).dispatch(width, height)
           << output_buffer.download_sync(pixels.data()) << ocarina::commit();
    display.present(pixels, width, height, HardwareImage::Format::RGBA8_UNORM);
}
