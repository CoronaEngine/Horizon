#include "Horizon.h"
#include "dsl/dsl.h"
#include "rhi/common.h"
#include "rhi/context.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#undef STB_IMAGE_WRITE_IMPLEMENTATION
#undef STB_IMAGE_WRITE_STATIC

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#undef STBI_ONLY_PNG
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_STATIC
#include <tiny_obj_loader.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#include <mach-o/dyld.h>
#endif
#include <GLFW/glfw3native.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

void run_example_baseline_ocarina() {
    using ocarina::Float, ocarina::Float3, ocarina::cast, ocarina::make_float3;
    using ocarina::commit, ocarina::synchronize;
    constexpr auto mode = "baseline_ocarina";
    constexpr bool hidden = false;
    constexpr float fixed_dt = 0.0f;
    constexpr char capture_path[] = "";

    // Slang builtins and the dynamically loaded backend live next to the executable.
#ifdef _WIN32
    std::wstring executable_name(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable_name.data(), static_cast<DWORD>(executable_name.size()));
    executable_name.resize(length);
    const auto executable = std::filesystem::path(executable_name);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string executable_name(size, '\0');
    _NSGetExecutablePath(executable_name.data(), &size);
    const auto executable = std::filesystem::canonical(executable_name.c_str());
#else
    const auto executable = std::filesystem::read_symlink("/proc/self/exe");
#endif
    auto device = ocarina::RHIContext::instance(executable.parent_path()).create_device();
    device.init_rtx();
    auto stream = device.create_stream();

#if defined(__linux__)
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    glfwInit();
    struct GlfwSession {
        ~GlfwSession() { glfwTerminate(); } // Also destroys the window.
    } glfw;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, !hidden);
    constexpr int width = 800, height = 600;
    GLFWwindow* window = glfwCreateWindow(width, height, mode, nullptr, nullptr);

#if defined(_WIN32)
    void* native_window = glfwGetWin32Window(window);
#elif defined(__linux__)
    void* native_window = reinterpret_cast<void*>(glfwGetX11Window(window));
#elif defined(__APPLE__)
    void* native_window = glfwGetCocoaView(window);
#endif
    HardwareDisplayer display(native_window, PresentDevicePreference::RenderDevice);

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

    auto vertices = device.create_buffer<ocarina::float3>(positions.size());
    auto indices = device.create_buffer<ocarina::uint3>(triangles.size());
    auto texcoords = device.create_buffer<ocarina::float2>(uvs.size());

    const auto filename = (assets / "textures/viking_room.png").string();
    int texture_width = 0, texture_height = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> texels(
        stbi_load(filename.c_str(), &texture_width, &texture_height, nullptr, STBI_rgb_alpha), stbi_image_free);
    auto texture = device.create_texture2d(ocarina::make_uint2(texture_width, texture_height), ocarina::PixelStorage::BYTE4);
    stream << vertices.upload(positions.data()) << indices.upload(triangles.data())
           << texcoords.upload(uvs.data()) << texture.upload(texels.get()) << synchronize() << commit();
    texels.reset();

    auto mesh = device.create_mesh(vertices, indices);
    stream << mesh.build_bvh() << synchronize() << commit();
    auto scene = device.create_accel();
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
    auto shader = device.compile(trace, mode);
    ocarina::vector<uint32_t> pixels(static_cast<size_t>(width) * height);
    auto output = device.create_buffer<uint32_t>(pixels.size());
    double start = 0.0;
    uint64_t frame = 0;
    for (;;) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) break;
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            glfwWaitEvents();
            continue;
        }
        if (frame == 0) start = glfwGetTime();
        const float seconds = fixed_dt ? static_cast<float>(frame) * fixed_dt : static_cast<float>(glfwGetTime() - start);
        constexpr float pi = std::numbers::pi_v<float>;
        const float angle = pi / 4.f - seconds * pi / 2.f;
        const float radius = std::sqrt(8.f); // Start at (2, 2, 2), as in the raster example.
        auto eye = make_float3(radius * std::cos(angle), radius * std::sin(angle), 2.f);
        stream << shader(eye, output).dispatch(width, height) << output.download_sync(pixels.data()) << commit();

        display.present(pixels, width, height, ImageFormat::RGBA8_SRGB);
        ++frame;
    }

    if (capture_path[0] && frame > 0) {
        const auto parent = std::filesystem::path(capture_path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        stbi_write_png(capture_path, width, height, 4, pixels.data(), width * 4);
    }
}
