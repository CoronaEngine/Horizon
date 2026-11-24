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

#include <GLFW/glfw3.h>
#include <stdexcept>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
static constexpr auto native_window_handle = glfwGetWin32Window;
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#include <mach-o/dyld.h>
static constexpr auto native_window_handle = glfwGetCocoaView;
#else
static void* native_window_handle(GLFWwindow*) {
    throw std::runtime_error("Native Horizon window handles are unavailable on this platform");
}
#endif

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

void run_example_baseline_ocarina() {
    using ocarina::Float, ocarina::Float3, ocarina::cast, ocarina::make_float3;
    using ocarina::commit, ocarina::synchronize;
    constexpr auto mode = "baseline_ocarina";

    // Slang builtins and the dynamically loaded backend live next to the executable.
#ifdef _WIN32
    std::wstring executable_name(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable_name.data(), static_cast<DWORD>(executable_name.size()));
    if (!length || length >= executable_name.size())
        throw std::runtime_error("Cannot locate HorizonExamples");
    executable_name.resize(length);
    const auto executable = std::filesystem::path(executable_name);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string executable_name(size, '\0');
    if (_NSGetExecutablePath(executable_name.data(), &size))
        throw std::runtime_error("Cannot locate HorizonExamples");
    const auto executable = std::filesystem::canonical(executable_name.c_str());
#else
    const auto executable = std::filesystem::read_symlink("/proc/self/exe");
#endif
    auto device = ocarina::RHIContext::instance(executable.parent_path()).create_device();
    if (!device.supports_ray_queries())
        throw std::runtime_error("Ocarina ray queries unsupported on this device");
    device.init_rtx();
    auto stream = device.create_stream();

    const auto env_int = [](const char* name) {
        const char* value = std::getenv(name);
        if (!value || !*value) return 0;
        char* end{};
        const long parsed = std::strtol(value, &end, 10);
        if (end == value || *end || parsed < 0 || parsed > 10000000)
            throw std::invalid_argument(std::string("Invalid ") + name);
        return static_cast<int>(parsed);
    };
    const bool hidden = env_int("HORIZON_BENCH_HIDDEN") != 0;
    int frames_remaining = env_int("HORIZON_PRESENT_FRAMES");
    float fixed_dt = 0.0f;
    if (const char* value = std::getenv("HORIZON_FIXED_DT"); value && *value) {
        fixed_dt = std::stof(value);
        if (!std::isfinite(fixed_dt) || fixed_dt < 0) throw std::invalid_argument("Invalid HORIZON_FIXED_DT");
    }

    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    struct GlfwSession {
        ~GlfwSession() { glfwTerminate(); } // Also destroys the window.
    } glfw;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    if (hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(1920, 1080, mode, nullptr, nullptr);
    if (!window) throw std::runtime_error("glfwCreateWindow failed");

    HardwareDisplayer display(native_window_handle(window), PresentDevicePreference::RenderDevice);

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
        std::string warn, err;
        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str(), material_dir.c_str()))
            throw std::runtime_error(warn + err);
        if (!warn.empty()) std::cerr << warn << '\n';

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
                        if (positions.size() >= 0x10000u)
                            throw std::runtime_error("Mesh exceeds the 65536-vertex 16-bit index limit: " + filename);
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
    if (!texels)
        throw std::runtime_error("Failed to load texture " + filename + ": " + stbi_failure_reason());
    auto texture = device.create_texture2d(ocarina::make_uint2(texture_width, texture_height), ocarina::PixelStorage::BYTE4);
    stream << vertices.upload(positions.data()) << indices.upload(triangles.data())
           << texcoords.upload(uvs.data()) << texture.upload(texels.get()) << synchronize() << commit();
    texels.reset();

    auto mesh = device.create_mesh(vertices.view(), indices.view(), ocarina::FAST_TRACE, ocarina::DISABLE_ANYHIT);
    stream << mesh.build_bvh() << synchronize() << commit();
    auto scene = device.create_accel(ocarina::FAST_TRACE);
    scene.add_instance(ocarina::move(mesh), ocarina::make_float4x4(1.f));
    stream << scene.build_bvh() << synchronize() << commit();

    // Ocarina compiles this function for the GPU. Each invocation writes one pixel.
    ocarina::Kernel trace = [&](Float3 eye, ocarina::BufferVar<uint32_t> output) {
        ocarina::Uint2 xy = ocarina::dispatch_idx().xy();
        ocarina::Uint2 extent = ocarina::dispatch_dim().xy();
        $if (xy.x >= extent.x || xy.y >= extent.y) { $return(); };
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
    int width = 0, height = 0;
    ocarina::vector<uint32_t> pixels;
    ocarina::Buffer<uint32_t> output;
    double start = 0.0;
    uint64_t frame = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        if (w == 0 || h == 0) {
            if (!glfwWindowShouldClose(window)) glfwWaitEvents();
            continue;
        }
        if (w != width || h != height) {
            width = w;
            height = h;
            pixels.resize(static_cast<size_t>(width) * height);
            output = device.create_buffer<uint32_t>(pixels.size());
        }
        if (frame == 0) start = glfwGetTime();
        const float seconds = fixed_dt ? static_cast<float>(frame) * fixed_dt : static_cast<float>(glfwGetTime() - start);
        constexpr float pi = std::numbers::pi_v<float>;
        const float angle = pi / 4.f - seconds * pi / 2.f;
        const float radius = std::sqrt(8.f); // Start at (2, 2, 2), as in the raster example.
        auto eye = make_float3(radius * std::cos(angle), radius * std::sin(angle), 2.f);
        stream << shader(eye, output).dispatch(width, height) << output.download(pixels.data()) << synchronize()
               << commit();

        const bool shown = display.present(pixels, width, height, ImageFormat::RGBA8_SRGB);
        ++frame;
        if (shown && frames_remaining > 0 && --frames_remaining == 0) break;
    }

    if (const char* path = std::getenv("HORIZON_CAPTURE_PATH"); path && *path && !pixels.empty()) {
        const auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        if (!stbi_write_png(path, width, height, 4, pixels.data(), width * 4))
            throw std::runtime_error("Could not write ray-traced PNG");
    }
}
