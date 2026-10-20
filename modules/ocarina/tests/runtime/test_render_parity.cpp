#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "math/transform.h"
#include "rhi/common.h"
#include "rhi/context.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>

using namespace ocarina;

namespace {
constexpr uint width = 96, height = 64, pixel_count = width * height;
const array<float3, 8> vertices{
    make_float3(-.5f, -.5f, -.5f), make_float3(.5f, -.5f, -.5f),
    make_float3(.5f, .5f, -.5f), make_float3(-.5f, .5f, -.5f),
    make_float3(-.5f, -.5f, .5f), make_float3(.5f, -.5f, .5f),
    make_float3(.5f, .5f, .5f), make_float3(-.5f, .5f, .5f)};
const array<uint3, 12> triangles{
    make_uint3(0, 2, 1), make_uint3(0, 3, 2), make_uint3(4, 5, 6), make_uint3(4, 6, 7),
    make_uint3(0, 1, 5), make_uint3(0, 5, 4), make_uint3(3, 7, 6), make_uint3(3, 6, 2),
    make_uint3(0, 4, 7), make_uint3(0, 7, 3), make_uint3(1, 2, 6), make_uint3(1, 6, 5)};
const array<float3, 4> hemisphere_samples{
    normalize(make_float3(.6f, .2f, .7f)), normalize(make_float3(-.4f, .5f, .65f)),
    normalize(make_float3(.2f, -.7f, .6f)), normalize(make_float3(-.65f, -.3f, .55f))};
const float3 light = normalize(make_float3(-.4f, .9f, .35f));

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(EXIT_FAILURE); }
}

struct CpuHit { uint triangle = ~0u; float t = ray_t_max; float2 bary{}; };

// Independent Moller-Trumbore reference, evaluated against world-space triangles.
CpuHit intersect(const vector<float3> &world, float3 origin, float3 direction,
                 float t_max = ray_t_max) {
    CpuHit closest; closest.t = t_max;
    for (uint i = 0; i < world.size(); i += 3) {
        const auto e1 = world[i + 1] - world[i];
        const auto e2 = world[i + 2] - world[i];
        const auto p = cross(direction, e2);
        const float det = dot(e1, p);
        if (std::abs(det) < 1e-8f) continue;
        const auto s = origin - world[i];
        const float u = dot(s, p) / det;
        const auto q = cross(s, e1);
        const float v = dot(direction, q) / det;
        const float t = dot(e2, q) / det;
        if (u >= 0 && v >= 0 && u + v <= 1 && t >= 0 && t < closest.t)
            closest = {i / 3, t, make_float2(1.f - u - v, u)};
    }
    return closest;
}

float3 sky(float3 d) { return make_float3(.12f, .18f, .3f) + .1f * (d + 1.f); }

float3 shade_cpu(const vector<float3> &world, const vector<float3> &colors,
                 const Ray &ray, const CpuHit &hit) {
    if (hit.triangle == ~0u) return sky(ray.direction());
    const uint j = hit.triangle * 3;
    const float3 p = triangle_lerp(hit.bary, world[j], world[j + 1], world[j + 2]);
    float3 n = normalize(cross(world[j + 1] - world[j], world[j + 2] - world[j]));
    if (dot(n, ray.direction()) > 0) n = -n;
    const auto origin = offset_ray_origin(p, n);
    const float visible = intersect(world, origin, light, 100.f).triangle == ~0u ? 1.f : 0.f;
    float3 bx, by;
    coordinate_system(n, bx, by);
    float3 indirect = make_float3(0.f);
    for (auto s : hemisphere_samples) {
        const float3 direction = normalize(bx * s.x + by * s.y + n * s.z);
        if (intersect(world, origin, direction, 100.f).triangle == ~0u)
            indirect += sky(direction) * .25f;
    }
    return colors[hit.triangle / 12] * (.08f + .3f * indirect + .7f * std::max(0.f, dot(n, light)) * visible);
}

void save_image(const char *scene, const vector<float4> &pixels) {
    const char *directory = std::getenv("OCARINA_RENDER_OUTPUT_DIR");
    if (!directory || !*directory) return;
    const fs::path root(directory);
    fs::create_directories(root);
    const string name = string(scene) + "-" + test_backend_name() + "-" +
        (Env::shader_codegen_path() == ShaderCodegenPath::EAstToIR ? "ir" : "ast");
    std::ofstream raw(root / (name + ".f32"), std::ios::binary);
    raw.write(reinterpret_cast<const char *>(pixels.data()), pixels.size() * sizeof(float4));
    std::ofstream ppm(root / (name + ".ppm"), std::ios::binary);
    ppm << "P6\n" << width << ' ' << height << "\n255\n";
    for (auto pixel : pixels) for (uint c = 0; c < 3; ++c) {
        const auto value = static_cast<unsigned char>(std::lround(std::sqrt(std::clamp(pixel[c], 0.f, 1.f)) * 255.f));
        ppm.write(reinterpret_cast<const char *>(&value), 1);
    }
}

vector<float3> world_triangles(const vector<float4x4> &transforms) {
    vector<float3> world;
    for (const auto &transform : transforms) for (auto triangle : triangles)
        for (uint c = 0; c < 3; ++c)
            world.push_back((transform * make_float4(vertices[triangle[c]], 1.f)).xyz());
    return world;
}

void render(Device &device, Stream &stream, const Accel &accel,
            const vector<float4x4> &transforms, const char *name) {
    const auto world = world_triangles(transforms);
    const vector<float3> colors{make_float3(.72f, .68f, .6f), make_float3(.8f, .15f, .1f), make_float3(.1f, .35f, .8f)};
    vector<Ray> rays;
    const float3 camera = make_float3(3.4f, 2.5f, 5.2f);
    const float3 forward = normalize(make_float3(0.f, -.05f, 0.f) - camera);
    const float3 right = normalize(cross(forward, make_float3(0.f, 1.f, 0.f)));
    const float3 up = cross(right, forward);
    for (uint y = 0; y < height; ++y) for (uint x = 0; x < width; ++x) {
        const float u = ((x + .371f) / width * 2.f - 1.f) * 1.5f * .48f;
        const float v = (1.f - (y + .613f) / height * 2.f) * .48f;
        rays.emplace_back(camera, normalize(forward + right * u + up * v));
    }
    auto ray_buffer = device.create_buffer<Ray>(pixel_count, "render-primary-rays");
    auto world_buffer = device.create_buffer<float3>(world.size(), "render-world-triangles");
    auto color_buffer = device.create_buffer<float3>(colors.size(), "render-materials");
    auto hit_buffer = device.create_buffer<TriangleHit>(pixel_count, "render-primary-hits");
    auto output = device.create_buffer<float4>(pixel_count, "render-color");
    stream << ray_buffer.upload(rays.data()) << world_buffer.upload(world.data()) << color_buffer.upload(colors.data());
    Kernel kernel = [&accel](BufferVar<Ray> ray_data, BufferVar<float3> world_data,
                             BufferVar<float3> materials, BufferVar<TriangleHit> hits, BufferVar<float4> image) {
        Uint i = dispatch_id();
        Var<Ray> ray = ray_data.read(i);
        TriangleHitVar hit = accel.trace_closest(ray);
        hits.write(i, hit);
        Float3 color = make_float3(.12f, .18f, .3f) + .1f * (ray->direction() + 1.f);
        $if(hit->is_hit()) {
            Uint j = hit->inst_id * 36u + hit->prim_id * 3u;
            Float3 a = world_data.read(j), b = world_data.read(j + 1u), c = world_data.read(j + 2u);
            Float3 p = hit->triangle_lerp(a, b, c);
            Float3 n = normalize(cross(b - a, c - a));
            $if(dot(n, ray->direction()) > 0.f) { n = -n; };
            Float3 origin = offset_ray_origin(p, n);
            Bool occluded = accel.trace_occlusion(make_ray(origin, light, 100.f));
            Float3 bx, by;
            coordinate_system(n, bx, by);
            Float3 indirect = make_float3(0.f);
            for (auto sample : hemisphere_samples) {
                Float3 direction = normalize(bx * sample.x + by * sample.y + n * sample.z);
                $if(!accel.trace_occlusion(make_ray(origin, direction, 100.f))) {
                    indirect += (make_float3(.12f, .18f, .3f) + .1f * (direction + 1.f)) * .25f;
                };
            }
            color = materials.read(hit->inst_id) * (.08f + .3f * indirect +
                .7f * max(0.f, dot(n, light)) * select(occluded, 0.f, 1.f));
        };
        image.write(i, make_float4(color, 1.f));
    };
    auto shader = device.compile(kernel, "box-render-parity");
    vector<float4> pixels(pixel_count);
    vector<TriangleHit> hits(pixel_count);
    stream << shader(ray_buffer, world_buffer, color_buffer, hit_buffer, output).dispatch(pixel_count)
           << output.download(pixels.data()) << hit_buffer.download(hits.data()) << synchronize() << commit();
    save_image(name, pixels);
    uint hit_count = 0, mismatches = 0;
    float max_error = 0.f;
    for (uint i = 0; i < pixel_count; ++i) {
        const auto expected_hit = intersect(world, rays[i].origin(), rays[i].direction());
        const bool expected_miss = expected_hit.triangle == ~0u;
        require(hits[i].is_miss() == expected_miss, "render primary hit/miss differs from CPU reference");
        if (!expected_miss) {
            ++hit_count;
            require(hits[i].inst_id == expected_hit.triangle / 12, "render instance ID differs from CPU reference");
        }
        const auto expected = shade_cpu(world, colors, rays[i], expected_hit);
        for (uint c = 0; c < 3; ++c) {
            const float error = std::abs(pixels[i][c] - expected[c]);
            max_error = std::max(max_error, error);
            if (!std::isfinite(pixels[i][c]) || error > 1e-4f) {
                if (mismatches < 5) std::cerr << name << " pixel=" << i << " channel=" << c
                    << " actual=" << pixels[i][c] << " expected=" << expected[c] << '\n';
                ++mismatches;
            }
        }
        require(pixels[i].w == 1.f, "render alpha differs");
    }
    std::cout << "RENDER_PARITY " << name << " pixels=" << pixel_count << " hits=" << hit_count
              << " mismatched_channels=" << mismatches << " max_abs_error=" << max_error << '\n';
    require(hit_count > 100 && hit_count < pixel_count, "render scene must contain foreground and background");
    require(mismatches == 0, "render RGB differs from independent CPU reference");
}

void scene(Device &device, vector<float4x4> transforms, bool update) {
    auto stream = device.create_stream();
    auto vb = device.create_buffer<float3>(vertices.size(), "box-vertices");
    auto ib = device.create_buffer<uint3>(triangles.size(), "box-triangles");
    stream << vb.upload(vertices.data()) << ib.upload(triangles.data()) << synchronize() << commit();
    auto accel = device.create_accel(FAST_UPDATE);
    for (auto transform : transforms) {
        auto mesh = device.create_mesh(vb.view(), ib.view(), FAST_TRACE, DISABLE_ANYHIT);
        stream << mesh.build_bvh() << synchronize() << commit();
        accel.add_instance(ocarina::move(mesh), transform);
    }
    stream << accel.build_bvh() << synchronize() << commit();
    render(device, stream, accel, transforms, update ? "boxes" : "single-box");
    if (update) {
        transforms[1] = translation<H>(-.5f, .25f, .55f) * rotation_y<H>(-31.f) * scale<H>(.8f, 1.5f, .7f);
        accel.set_transform(1u, transforms[1]);
        stream << accel.update_bvh() << synchronize() << commit();
        render(device, stream, accel, transforms, "boxes-refit");
    }
}
} // namespace

int main() {
    auto device = create_test_device();
    device.init_rtx();
    scene(device, {make_float4x4(1.f)}, false);
    scene(device, {translation<H>(0.f, -.65f, 0.f) * scale<H>(5.f, .2f, 4.f),
                   translation<H>(-.65f, -.05f, .15f) * rotation_y<H>(23.f) * scale<H>(.9f, 1.f, .7f),
                   translation<H>(.65f, .1f, -.35f) * rotation_y<H>(-17.f) * scale<H>(.75f, 1.3f, .85f)}, true);
}
