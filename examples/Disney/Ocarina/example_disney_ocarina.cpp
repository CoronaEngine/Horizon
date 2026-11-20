#include "../scene.h"
#include "lighting.h"
#include "window.h"
#include "math/transform.h"
#include <GLFW/glfw3.h>

void run_example_disney_ocarina() {
    using namespace ocarina;
    constexpr auto mode = "disney_ocarina";
    auto device = ray_example::open_device();
    auto stream = device.create_stream();
    auto window = examples::open_window(ray_view::width, ray_view::height, mode);
    disney::InputContext input;
    // Leave a margin around all ten rows at the initial orbit angle.
    input.camera.dist_curr = input.camera.dist_dest = 15.f;
    disney::attach_input(window.get(), input);

    auto radiance = disney_rt::load_environment(device, "bolonga_lod.dds");
    auto irradiance = disney_rt::load_environment(device, "bolonga_irr.dds");
    const auto sphere = disney::make_uv_sphere(48, 24);
    vector<float3> positions;
    for (auto p : sphere.vertices)
        positions.push_back(make_float3(p[0], p[1], p[2]));
    vector<uint3> triangles;
    // Convert the raster triangle strip to a triangle list, dropping connector triangles.
    for (size_t i = 2; i < sphere.indices.size(); ++i) {
        uint a = sphere.indices[i - 2], b = sphere.indices[i - 1], c = sphere.indices[i];
        if (a != b && b != c && c != a)
            triangles.push_back(i % 2 ? make_uint3(b, a, c) : make_uint3(a, b, c));
    }
    auto vertices = device.create_buffer<float3>(positions.size());
    auto indices = device.create_buffer<uint3>(triangles.size());
    stream << vertices.upload(positions.data()) << indices.upload(triangles.data()) << synchronize() << commit();
    auto mesh = device.create_mesh(vertices.view(), indices.view(), FAST_TRACE, DISABLE_ANYHIT);
    stream << mesh.build_bvh() << synchronize() << commit();
    auto chart = device.create_accel(FAST_TRACE);

    // Each instance gets three chart material vectors, followed by its XY centre, radius and clearcoat gloss.
    vector<float4> material_data;
    for (int row = 0; row < disney::chart_row_count; ++row) {
        for (int col = 0; col < disney::chart_col_count; ++col) {
            const auto sphere_params = disney::sphere_parameters(row, col);
            const auto centre = sphere_params.model[3];
            const float radius = sphere_params.model[0][0];
            const auto transform = translation<H>(centre.x, centre.y, centre.z) * scale<H>(radius);
            if (row == 0 && col == 0)
                chart.add_instance(ocarina::move(mesh), transform);
            else
                chart.add_instance(0u, transform);
            for (auto v : {sphere_params.mat0, sphere_params.mat1, sphere_params.mat2})
                material_data.push_back(make_float4(v.x, v.y, v.z, v.w));
            material_data.push_back(make_float4(centre.x, centre.y, radius, sphere_params.clearcoatGloss));
        }
    }
    auto materials = device.create_buffer<float4>(material_data.size());
    stream << materials.upload(material_data.data()) << chart.build_bvh() << synchronize() << commit();

    Kernel trace = [&](Float3 eye, Float3 light, Float angle, BufferVar<uint> output) {
        Uint pixel = dispatch_id();
        Float3 forward = normalize(-eye);
        Float3 right = normalize(cross(make_float3(0.f, 1.f, 0.f), forward));
        auto ray = ray_example::camera_ray(pixel, eye, forward, right, cross(forward, right));
        auto hit = chart.trace_closest(ray);
        Float3 color = make_float3(.5f);
        $if (hit->is_hit()) {
            Uint3 tri = indices.read(hit->prim_id);
            Float3 local = hit->triangle_lerp(vertices.read(tri.x), vertices.read(tri.y), vertices.read(tri.z));
            Uint start = 4u * hit->inst_id;
            Float4 m0 = materials.read(start), m1 = materials.read(start + 1u), m2 = materials.read(start + 2u);
            Float4 placement = materials.read(start + 3u);
            Float3 point = make_float3(placement.x, placement.y, 0.f) + placement.z * local;
            Float3 n = normalize(local), v = -ray->direction();
            Float3 base = pow(m0.xyz(), make_float3(2.2f));
            Float3 tint = base / max(dot(base, make_float3(.3f, .6f, .1f)), .0001f);
            Float3 spec = lerp(m0.w, m1.y * .08f * lerp(m1.z, make_float3(1.f), tint), base);
            Float3 direct = make_float3(0.f);
            $if (dot(n, light) > 0.f) {
                Bool shadow = chart.trace_occlusion(make_ray(offset_ray_origin(point, n), light));
                direct = disney_rt::direct_brdf(n, v, light, base, spec, m0, m1, m2, placement.w) *
                         max(dot(n, light), 0.f) * select(shadow, 0.f, 1.f);
            };
            auto rotate_env = [&](Float3 d) {
                return make_float3(cos(angle) * d.x + sin(angle) * d.z, d.y,
                                   -sin(angle) * d.x + cos(angle) * d.z);
            };
            Float3 reflected = 2.f * dot(n, v) * n - v;
            Float3 diffuse_env = disney_rt::sample_environment(irradiance, rotate_env(n), 0.f);
            Float3 spec_env = disney_rt::sample_environment(radiance, rotate_env(reflected), m1.x * 5.f);
            Float fresnel = pow(1.f - clamp(dot(n, v), 0.f, 1.f), 5.f);
            Float3 lit =
                direct + base * (1.f - m0.w) * diffuse_env + lerp(fresnel, spec, make_float3(1.f)) * spec_env;
            Float3 x = max(lit - .004f, make_float3(0.f));
            color = x * (6.2f * x + .5f) / (x * (6.2f * x + 1.7f) + .06f);
        };
        output.write(pixel, ray_example::rgba8(color));
    };
    auto shader = device.compile(trace, mode);
    vector<uint> pixels(ray_view::width * ray_view::height);
    auto output = device.create_buffer<uint>(pixels.size());
    auto display = ray_view::open(window.get(), mode);
    float previous = 0;
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        const float now = ray_view::elapsed_seconds(*display);
        const auto frame = disney::update_frame(input, now - previous);
        previous = now;
        const auto eye = make_float3(frame.camera.x, frame.camera.y, frame.camera.z);
        const auto light = make_float3(frame.light_direction.x, frame.light_direction.y, frame.light_direction.z);
        stream << shader(eye, light, input.settings.env_rot_curr, output).dispatch(pixels.size())
               << output.download(pixels.data()) << synchronize() << commit();
        ray_view::present(*display, pixels);
    }
}
