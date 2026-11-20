#include "../scene.h"
#include "Ocarina/rays.h"
#include "window.h"
#include "example_benchmark/benchmark.h"
#include "math/transform.h"
#include <GLFW/glfw3.h>
#include <algorithm>

void run_example_drawstress_ocarina() {
    using namespace ocarina;
    constexpr auto mode = "drawstress_ocarina";
    auto device = ray_example::open_device();
    auto stream = device.create_stream();
    auto window = examples::open_window(ray_view::width, ray_view::height, mode);
    int dim = std::clamp(bench::env_int("HORIZON_DRAWSTRESS_DIM", 40), drawstress::dim_min, drawstress::dim_max);
    drawstress::attach_input(window.get(), dim, mode);

    vector<float3> corners;
    for (uint i = 0; i < 8; ++i)
        corners.push_back(make_float3((i & 1) ? 1.f : -1.f, (i & 2) ? -1.f : 1.f, (i & 4) ? -1.f : 1.f));
    vector<uint3> triangles;
    const auto cube_indices = drawstress::cube_triangle_indices();
    for (size_t i = 0; i < cube_indices.size(); i += 3)
        triangles.push_back(make_uint3(cube_indices[i], cube_indices[i + 1], cube_indices[i + 2]));
    auto vertices = device.create_buffer<float3>(corners.size());
    auto indices = device.create_buffer<uint3>(triangles.size());
    stream << vertices.upload(corners.data()) << indices.upload(triangles.data()) << synchronize() << commit();

    auto grid = device.create_accel(FAST_UPDATE);
    Kernel trace = [&](Float3 eye, BufferVar<uint> output) {
        Uint pixel = dispatch_id();
        auto ray = ray_example::camera_ray(pixel, eye, make_float3(0.f, 0.f, 1.f), make_float3(1.f, 0.f, 0.f),
                                           make_float3(0.f, 1.f, 0.f));
        auto hit = grid.trace_closest(ray);
        Float3 color = make_float3(.19f);
        $if (hit->is_hit()) {
            Uint3 tri = indices.read(hit->prim_id);
            Float3 local = hit->triangle_lerp(vertices.read(tri.x), vertices.read(tri.y), vertices.read(tri.z));
            color = local * make_float3(.5f, -.5f, -.5f) + .5f;
        };
        output.write(pixel, ray_example::rgba8(color));
    };
    auto shader = device.compile(trace, mode);
    vector<uint> pixels(ray_view::width * ray_view::height);
    auto output = device.create_buffer<uint>(pixels.size());
    auto display = ray_view::open(window.get(), mode);
    int built_dim = 0;
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        const float time = ray_view::elapsed_seconds(*display);
        const bool rebuild = dim != built_dim;
        if (rebuild) {
            grid.clear();
            auto cube = device.create_mesh(vertices.view(), indices.view(), FAST_TRACE, DISABLE_ANYHIT);
            stream << cube.build_bvh() << synchronize() << commit();
            grid.add_instance(ocarina::move(cube), make_float4x4(1.f));
        }
        for (int z = 0; z < dim; ++z) {
            for (int y = 0; y < dim; ++y) {
                for (int x = 0; x < dim; ++x) {
                    const uint instance = (z * dim + y) * dim + x;
                    const auto centre = (make_float3(float(x), float(y), float(z)) - .5f * (dim - 1)) * .6f;
                    const auto transform = translation<H>(centre) * rotation_z<H>(time + z * .13f, true) *
                                           rotation_y<H>(time + y * .37f, true) *
                                           rotation_x<H>(time + x * .21f, true) * scale<H>(.25f);
                    if (rebuild && instance != 0)
                        grid.add_instance(0u, transform);
                    else
                        grid.set_transform(instance, transform);
                }
            }
        }
        // A dimension change replaces the instance list. Animation only refits the existing TLAS.
        if (rebuild)
            stream << grid.build_bvh();
        else
            stream << grid.update_bvh();
        built_dim = dim;
        auto eye = make_float3(0.f, 0.f, -float(dim) * 1.05f);
        stream << shader(eye, output).dispatch(pixels.size()) << output.download(pixels.data()) << synchronize()
               << commit();
        ray_view::present(*display, pixels);
    }
}
