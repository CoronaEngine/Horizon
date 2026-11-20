#include "../assets.h"
#include "Ocarina/rays.h"
#include "window.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <numbers>

void run_example_baseline_ocarina() {
    using namespace ocarina;
    constexpr auto mode = "baseline_ocarina";
    auto device = ray_example::open_device();
    auto stream = device.create_stream();
    auto window = examples::open_window(ray_view::width, ray_view::height, mode);

    const auto model = baseline::read_model("models/viking_room.obj");
    const auto image = baseline::read_texture("textures/viking_room.png");
    vector<float3> positions;
    vector<float2> uvs;
    vector<uint3> triangles;
    for (const auto& v : model.vertices) {
        positions.push_back(make_float3(v.pos[0], v.pos[1], v.pos[2]));
        uvs.push_back(make_float2(v.tex_coord[0], v.tex_coord[1]));
    }
    for (size_t i = 0; i < model.indices.size(); i += 3)
        triangles.push_back(make_uint3(model.indices[i], model.indices[i + 1], model.indices[i + 2]));

    auto vertices = device.create_buffer<float3>(positions.size());
    auto indices = device.create_buffer<uint3>(triangles.size());
    auto texcoords = device.create_buffer<float2>(uvs.size());
    auto texture = device.create_texture2d(make_uint2(image.width, image.height), PixelStorage::BYTE4);
    stream << vertices.upload(positions.data()) << indices.upload(triangles.data())
           << texcoords.upload(uvs.data()) << texture.upload(image.rgba.data()) << synchronize() << commit();

    auto mesh = device.create_mesh(vertices.view(), indices.view(), FAST_TRACE, DISABLE_ANYHIT);
    stream << mesh.build_bvh() << synchronize() << commit();
    auto scene = device.create_accel(FAST_TRACE);
    scene.add_instance(ocarina::move(mesh), make_float4x4(1.f));
    stream << scene.build_bvh() << synchronize() << commit();

    // Ocarina compiles this function for the GPU. Each invocation writes one pixel.
    Kernel trace = [&](Float3 eye, BufferVar<uint> output) {
        Uint pixel = dispatch_id();
        Float3 forward = normalize(-eye);
        Float3 right = normalize(cross(forward, make_float3(0.f, 0.f, 1.f)));
        auto ray = ray_example::camera_ray(pixel, eye, forward, right, cross(right, forward));
        auto hit = scene.trace_closest(ray);
        Float3 color = make_float3(.055f, .075f, .10f);
        $if (hit->is_hit()) {
            Uint3 tri = indices.read(hit->prim_id);
            Float2 uv = hit->triangle_lerp(texcoords.read(tri.x), texcoords.read(tri.y), texcoords.read(tri.z));
            Float3 a = vertices.read(tri.x), b = vertices.read(tri.y), c = vertices.read(tri.z);
            Float3 point = hit->triangle_lerp(a, b, c);
            Float3 normal = normalize(cross(b - a, c - a));
            $if (dot(normal, ray->direction()) > 0.f) {
                normal = -normal;
            };
            const float3 light = normalize(make_float3(-.4f, .6f, 1.f));
            Bool in_shadow = scene.trace_occlusion(make_ray(offset_ray_origin(point, normal), light));
            Float illumination = .32f + .68f * max(dot(normal, light), 0.f) * select(in_shadow, 0.f, 1.f);
            color = texture.sample(4, uv).as_vec4().xyz() * sqrt(illumination);
        };
        output.write(pixel, ray_example::rgba8(color));
    };
    auto shader = device.compile(trace, mode);
    vector<uint> pixels(ray_view::width * ray_view::height);
    auto output = device.create_buffer<uint>(pixels.size());
    auto display = ray_view::open(window.get(), mode);
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        constexpr float pi = std::numbers::pi_v<float>;
        const float angle = pi / 4.f - ray_view::elapsed_seconds(*display) * pi / 2.f;
        const float radius = std::sqrt(8.f); // Start at (2, 2, 2), as in the raster example.
        auto eye = make_float3(radius * std::cos(angle), radius * std::sin(angle), 2.f);
        stream << shader(eye, output).dispatch(pixels.size()) << output.download(pixels.data()) << synchronize()
               << commit();
        ray_view::present(*display, pixels);
    }
}
