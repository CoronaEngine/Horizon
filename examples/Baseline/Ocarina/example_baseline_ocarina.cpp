#include "BaselineOcarina.h"

#include "dsl/dsl.h"
#include "math/ktm_bridge.h"
#include "rhi/context.h"

#include <ktm/ktm.h>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#undef STBI_ONLY_PNG
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_STATIC
#include <tiny_obj_loader.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

BaselineOcarina::BaselineOcarina(void* native_window)
    : ExampleBase(native_window), display(native_window, PresentDevicePreference::RenderDevice),
      device(ocarina::RHIContext::instance().create_device()), stream(device.create_stream())
{
    device.init_rtx();

    using ocarina::Float, ocarina::Float3, ocarina::cast, ocarina::make_float3;
    using ocarina::commit, ocarina::synchronize;

    const auto assets = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";
    ocarina::vector<ocarina::float3> positions;
    ocarina::vector<ocarina::float2> uvs;
    ocarina::vector<ocarina::uint3> triangles;
    {
        const auto filename = (assets / "models/viking_room.obj").string();
        const auto material_dir = (assets / "models/").string();
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        tinyobj::LoadObj(&attrib, &shapes, &materials, nullptr, nullptr, filename.c_str(), material_dir.c_str());

        using Vertex = std::array<float, 5>; // Position (x, y, z), then texture coordinates (u, v).
        std::map<Vertex, uint32_t> unique_vertices;
        for (const auto& shape : shapes) {
            // tinyobj triangulates the faces, so every three indices form one ray-tracing primitive.
            for (size_t face = 0; face < shape.mesh.indices.size(); face += 3) {
                ocarina::uint3 triangle;
                for (size_t corner = 0; corner < 3; ++corner) {
                    const auto& index = shape.mesh.indices[face + corner];
                    const size_t p = 3 * static_cast<size_t>(index.vertex_index);
                    Vertex vertex{attrib.vertices[p], attrib.vertices[p + 1], attrib.vertices[p + 2]};
                    if (index.texcoord_index >= 0) {
                        const size_t uv = 2 * static_cast<size_t>(index.texcoord_index);
                        vertex[3] = attrib.texcoords[uv];
                        vertex[4] = 1.0f - attrib.texcoords[uv + 1];
                    }
                    const auto [found, inserted] = unique_vertices.emplace(vertex, static_cast<uint32_t>(positions.size()));
                    if (inserted) {
                        positions.push_back(make_float3(vertex[0], vertex[1], vertex[2]));
                        uvs.push_back(ocarina::make_float2(vertex[3], vertex[4]));
                    }
                    triangle[corner] = found->second;
                }
                triangles.push_back(triangle);
            }
        }
    }

    vertices = device.create_buffer<ocarina::float3>(positions.size());
    indices = device.create_buffer<ocarina::uint3>(triangles.size());
    texcoords = device.create_buffer<ocarina::float2>(uvs.size());

    const auto filename = (assets / "textures/viking_room.png").string();
    int texture_width = 0, texture_height = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> texels(
        stbi_load(filename.c_str(), &texture_width, &texture_height, nullptr, STBI_rgb_alpha), stbi_image_free);
    texture = device.create_texture2d(ocarina::make_uint2(texture_width, texture_height), ocarina::PixelStorage::BYTE4);
    stream << vertices.upload(positions.data()) << indices.upload(triangles.data())
           << texcoords.upload(uvs.data()) << texture.upload(texels.get()) << synchronize() << commit();
    texels.reset();

    auto mesh = device.create_mesh(vertices, indices);
    stream << mesh.build_bvh() << synchronize() << commit();
    scene = device.create_accel();
    scene.add_instance(ocarina::move(mesh), ocarina::make_float4x4(1.f));
    stream << scene.build_bvh() << synchronize() << commit();

    // Ocarina compiles this function for the GPU. Each invocation writes one pixel.
    ocarina::Kernel trace = [&](Float3 eye, ocarina::BufferVar<uint32_t> output) {
        ocarina::Uint2 xy = ocarina::dispatch_idx().xy();
        ocarina::Uint2 extent = ocarina::dispatch_dim().xy();
        auto pixel = xy.y * extent.x + xy.x;
        Float3 forward = normalize(-eye);
        Float3 right = normalize(cross(forward, make_float3(0.f, 0.f, 1.f)));
        Float3 up = cross(right, forward);
        Float screen_x = (cast<float>(xy.x) + .5f) / ocarina::cast<float>(extent.x) * 2.f - 1.f;
        Float screen_y = 1.f - (cast<float>(xy.y) + .5f) / ocarina::cast<float>(extent.y) * 2.f;
        ocarina::Float aspect = ocarina::cast<float>(extent.x) / ocarina::cast<float>(extent.y);
        constexpr float half_fov = .41421356f; // tan(45 degrees / 2)
        auto ray = make_ray(eye, normalize(forward + half_fov * (right * screen_x * aspect + up * screen_y)));
        auto hit = scene.trace_closest(ray);
        Float3 color = make_float3(0.f);
        $if (hit->is_hit()) {
            ocarina::Uint3 tri = indices.read(hit->prim_id);
            ocarina::Float2 uv = hit->triangle_lerp(texcoords.read(tri.x), texcoords.read(tri.y), texcoords.read(tri.z));
            color = texture.sample(4, uv).as_vec4().xyz();
        };
        Float3 bytes = clamp(color, 0.f, 1.f) * 255.f + .5f;
        output.write(pixel, cast<uint32_t>(bytes.x) | (cast<uint32_t>(bytes.y) << 8u) |
                            (cast<uint32_t>(bytes.z) << 16u) | 0xff000000u);
    };
    shader = device.compile(trace, "baseline_ocarina");
    pixels.resize(width * height);
    output_buffer = device.create_buffer<uint32_t>(pixels.size());
}

void BaselineOcarina::run(float seconds)
{
    const float angle = ktm::radians(45.f) - seconds * ktm::half_pi<float>;
    const float radius = ktm::sqrt(8.f); // Start at (2, 2, 2), as in the raster example.
    const ktm::fvec3 eye{radius * ktm::cos(angle), radius * ktm::sin(angle), 2.f};
    stream << shader(ocarina::bridge::from_ktm(eye), output_buffer).dispatch(width, height)
           << output_buffer.download_sync(pixels.data()) << ocarina::commit();

    display.present(pixels, width, height, HardwareImage::Format::RGBA8_SRGB);
}
