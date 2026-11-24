#include "Horizon.h"
#include "dsl/dsl.h"
#include "rhi/common.h"
#include "rhi/context.h"
#include "math/transform.h"

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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#undef STB_IMAGE_WRITE_IMPLEMENTATION
#undef STB_IMAGE_WRITE_STATIC

void run_example_drawstress_ocarina() {
    using ocarina::Float3, ocarina::make_float3;
    using ocarina::cast, ocarina::commit, ocarina::synchronize;
    using ocarina::H;

    constexpr auto mode = "drawstress_ocarina";
    const auto env_int = [](const char* name, int fallback = 0) {
        const char* value = std::getenv(name);
        if (!value || !*value) return fallback;
        char* end{};
        const long parsed = std::strtol(value, &end, 10);
        if (end == value || *end || parsed < 0 || parsed > 10000000)
            throw std::invalid_argument(std::string("Invalid ") + name);
        return static_cast<int>(parsed);
    };
    constexpr int dim_max = 40;
    const int dim = std::clamp(env_int("HORIZON_DRAWSTRESS_DIM", dim_max), 2, dim_max);
    const bool hidden = env_int("HORIZON_BENCH_HIDDEN") != 0;
    int remaining_frames = env_int("HORIZON_PRESENT_FRAMES");
    float fixed_dt = 0.0f;
    if (const char* value = std::getenv("HORIZON_FIXED_DT"); value && *value) {
        fixed_dt = std::stof(value);
        if (!std::isfinite(fixed_dt) || fixed_dt < 0.0f)
            throw std::invalid_argument("Invalid HORIZON_FIXED_DT");
    }

    // Slang builtins and the dynamically loaded backend live next to the executable.
#ifdef _WIN32
    std::wstring filename(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, filename.data(), static_cast<DWORD>(filename.size()));
    if (!length || length >= filename.size())
        throw std::runtime_error("Cannot locate HorizonExamples");
    filename.resize(length);
    const auto executable = std::filesystem::path(filename);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string filename(size, '\0');
    if (_NSGetExecutablePath(filename.data(), &size))
        throw std::runtime_error("Cannot locate HorizonExamples");
    const auto executable = std::filesystem::canonical(filename.c_str());
#else
    const auto executable = std::filesystem::read_symlink("/proc/self/exe");
#endif
    auto device = ocarina::RHIContext::instance(executable.parent_path()).create_device();
    if (!device.supports_ray_queries())
        throw std::runtime_error("Ocarina ray queries unsupported on this device");
    device.init_rtx();
    auto stream = device.create_stream();

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
    auto vertices = device.create_buffer<ocarina::float3>(corners.size());
    auto indices = device.create_buffer<ocarina::uint3>(triangles.size());
    stream << vertices.upload(corners.data()) << indices.upload(triangles.data()) << synchronize() << commit();

    auto cube = device.create_mesh(vertices.view(), indices.view(), ocarina::FAST_TRACE, ocarina::DISABLE_ANYHIT);
    stream << cube.build_bvh() << synchronize() << commit();
    auto grid = device.create_accel(ocarina::FAST_UPDATE);
    grid.add_instance(ocarina::move(cube), ocarina::make_float4x4(1.f));
    for (int i = 1; i < dim * dim * dim; ++i)
        grid.add_instance(0u, ocarina::make_float4x4(1.f));

    ocarina::Kernel trace = [&](Float3 eye, ocarina::BufferVar<uint32_t> output) {
        ocarina::Uint2 xy = ocarina::dispatch_idx().xy();
        ocarina::Uint2 extent = ocarina::dispatch_dim().xy();
        $if (xy.x >= extent.x || xy.y >= extent.y) { $return(); };
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
    auto shader = device.compile(trace, mode);
    int width = 0, height = 0;
    ocarina::vector<uint32_t> pixels;
    ocarina::Buffer<uint32_t> output;
    const auto eye = make_float3(0.f, 0.f, -35.f);
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
        const float time = fixed_dt ? static_cast<float>(frame) * fixed_dt : static_cast<float>(glfwGetTime() - start);
        for (int z = 0; z < dim; ++z) {
            for (int y = 0; y < dim; ++y) {
                for (int x = 0; x < dim; ++x) {
                    const uint32_t instance = (z * dim + y) * dim + x;
                    const auto centre = make_float3(.6f * (x - .5f * dim), .6f * (y - .5f * dim), -15.f + .6f * z);
                    const auto transform = ocarina::translation<H>(centre)
                        * ocarina::rotation_z<H>(time + z * .13f, true) * ocarina::rotation_y<H>(time + y * .37f, true)
                        * ocarina::rotation_x<H>(time + x * .21f, true) * ocarina::scale<H>(.25f);
                    grid.set_transform(instance, transform);
                }
            }
        }
        if (frame == 0) {
            stream << grid.build_bvh();
        } else {
            stream << grid.update_bvh();
        }
        stream << shader(eye, output).dispatch(width, height) << output.download(pixels.data()) << synchronize()
               << commit();
        const bool shown = display.present(pixels, width, height, ImageFormat::RGBA8_UNORM);
        ++frame;
        if (shown && remaining_frames > 0 && --remaining_frames == 0) break;
    }

    if (const char* filename = std::getenv("HORIZON_CAPTURE_PATH"); filename && *filename && !pixels.empty()) {
        const auto parent = std::filesystem::path(filename).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        if (!stbi_write_png(filename, width, height, 4, pixels.data(), width * 4))
            throw std::runtime_error("Could not write ray-traced PNG");
    }
}
