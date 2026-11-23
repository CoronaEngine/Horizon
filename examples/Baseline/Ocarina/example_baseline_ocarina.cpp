#include "Horizon.h"
#include "dsl/dsl.h"
#include "rhi/common.h"
#include "rhi/context.h"
#include <GLFW/glfw3.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#undef STB_IMAGE_WRITE_IMPLEMENTATION
#undef STB_IMAGE_WRITE_STATIC
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#undef STBI_ONLY_PNG
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_STATIC
#include <tiny_obj_loader.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

static int env_int(const char* name, int fallback);

class BaselineRayDisplay {
public:
    explicit BaselineRayDisplay(GLFWwindow* window);
    ~BaselineRayDisplay() { copy_.waitForDeferredResources(); }

    float elapsed_seconds() {
        if (frame_ == 0) start_ = glfwGetTime();
        return fixed_dt_ ? static_cast<float>(frame_) * fixed_dt_
                         : static_cast<float>(glfwGetTime() - start_);
    }
    void resize(int width, int height);
    void present(std::span<const uint32_t> pixels, int width, int height);

private:
    GLFWwindow* window_;
    int remaining_frames_ = env_int("HORIZON_PRESENT_FRAMES", 0);
    HardwareDisplayer surface_;
    HardwareExecutor copy_;
    HardwareBuffer staging_;
    HardwareImage image_;
    double start_{};
    float fixed_dt_{};
    uint64_t frame_{};
};

static ocarina::Device open_device();

struct BaselineRayModel {
    struct Vertex {
        std::array<float, 3> pos{};
        std::array<float, 2> tex_coord{};
        bool operator==(const Vertex&) const = default;
    };
    static_assert(sizeof(Vertex) == 5 * sizeof(float));
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
};

const auto assets = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";
static BaselineRayModel read_model();

static auto open_window(int width, int height, const char* title) {
    const bool hidden = env_int("HORIZON_BENCH_HIDDEN", 0) != 0;
    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    if (hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }
    auto close = [](GLFWwindow* handle) {
        glfwDestroyWindow(handle);
        glfwTerminate();
    };
    return std::unique_ptr<GLFWwindow, decltype(close)>(window, close);
}

static void* native_handle(GLFWwindow* window) {
#if defined(_WIN32)
    return glfwGetWin32Window(window);
#elif defined(__APPLE__)
    return glfwGetCocoaView(window);
#else
    throw std::runtime_error("Native Horizon window handles are unavailable on this platform");
#endif
}

void run_example_baseline_ocarina() {
    using ocarina::Float, ocarina::Float3, ocarina::cast, ocarina::make_float3;
    using ocarina::commit, ocarina::synchronize;
    constexpr auto mode = "baseline_ocarina";
    auto device = open_device();
    auto stream = device.create_stream();
    auto window = open_window(1920, 1080, mode);

    const auto model = read_model();
    ocarina::vector<ocarina::float3> positions;
    ocarina::vector<ocarina::float2> uvs;
    ocarina::vector<ocarina::uint3> triangles;
    for (const auto& v : model.vertices) {
        positions.push_back(make_float3(v.pos[0], v.pos[1], v.pos[2]));
        uvs.push_back(ocarina::make_float2(v.tex_coord[0], v.tex_coord[1]));
    }
    for (size_t i = 0; i < model.indices.size(); i += 3)
        triangles.push_back(ocarina::make_uint3(model.indices[i], model.indices[i + 1], model.indices[i + 2]));

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
    BaselineRayDisplay display(window.get());
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        int w, h;
        glfwGetFramebufferSize(window.get(), &w, &h);
        if (w == 0 || h == 0) {
            if (!glfwWindowShouldClose(window.get())) glfwWaitEvents();
            continue;
        }
        if (w != width || h != height) {
            width = w;
            height = h;
            pixels.resize(static_cast<size_t>(width) * height);
            output = device.create_buffer<uint32_t>(pixels.size());
            display.resize(width, height);
        }
        constexpr float pi = std::numbers::pi_v<float>;
        const float angle = pi / 4.f - display.elapsed_seconds() * pi / 2.f;
        const float radius = std::sqrt(8.f); // Start at (2, 2, 2), as in the raster example.
        auto eye = make_float3(radius * std::cos(angle), radius * std::sin(angle), 2.f);
        stream << shader(eye, output).dispatch(width, height) << output.download(pixels.data()) << synchronize()
               << commit();
        display.present(pixels, width, height);
    }
}

static size_t hash_vertex(const BaselineRayModel::Vertex& vertex) {
    size_t seed = 0;
    auto combine = [&seed](float value) {
        seed ^= std::hash<float>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    };
    for (float value : vertex.pos) combine(value);
    for (float value : vertex.tex_coord) combine(value);
    return seed;
}

static BaselineRayModel read_model()
{
    using Vertex = BaselineRayModel::Vertex;
    const auto model_path = assets / "models/viking_room.obj";
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    const std::string model_path_str = model_path.string();
    const std::string material_base_path =
        model_path.parent_path().string() + std::string(1, std::filesystem::path::preferred_separator);

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, model_path_str.c_str(), material_base_path.c_str()))
        throw std::runtime_error(warn + err);
    if (!warn.empty())
        std::cerr << warn << '\n';

    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    std::unordered_map<Vertex, uint32_t, decltype(&hash_vertex)> unique_vertices(0, hash_vertex);
    for (const tinyobj::shape_t& shape : shapes)
    {
        for (const tinyobj::index_t& index : shape.mesh.indices)
        {
            Vertex vertex;
            vertex.pos = {
                attrib.vertices[3 * static_cast<size_t>(index.vertex_index) + 0],
                attrib.vertices[3 * static_cast<size_t>(index.vertex_index) + 1],
                attrib.vertices[3 * static_cast<size_t>(index.vertex_index) + 2],
            };
            if (index.texcoord_index >= 0)
            {
                vertex.tex_coord = {
                    attrib.texcoords[2 * static_cast<size_t>(index.texcoord_index) + 0],
                    1.0f - attrib.texcoords[2 * static_cast<size_t>(index.texcoord_index) + 1],
                };
            }

            auto [found, inserted] = unique_vertices.emplace(vertex, static_cast<uint32_t>(vertices.size()));
            if (inserted)
            {
                if (vertices.size() >= 0x10000u)
                    throw std::runtime_error("Mesh exceeds the 65536-vertex 16-bit index limit: " + model_path_str);
                vertices.push_back(vertex);
            }
            indices.push_back(static_cast<uint16_t>(found->second));
        }
    }

    return {std::move(vertices), std::move(indices)};
}

static int env_int(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    char* end{};
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end || parsed < 0 || parsed > 10000000)
        throw std::invalid_argument(std::string("Invalid ") + name);
    return static_cast<int>(parsed);
}

BaselineRayDisplay::BaselineRayDisplay(GLFWwindow* window)
    : window_(window), surface_(native_handle(window), PresentDevicePreference::RenderDevice) {
    if (const char* value = std::getenv("HORIZON_FIXED_DT"); value && *value) {
        fixed_dt_ = std::stof(value);
        if (!std::isfinite(fixed_dt_) || fixed_dt_ < 0)
            throw std::invalid_argument("Invalid HORIZON_FIXED_DT");
    }
}

void BaselineRayDisplay::resize(int width, int height) {
    copy_.waitForDeferredResources();
    staging_ = HardwareBuffer(width * height * 4u, BufferUsage::StorageBuffer);
    image_ = HardwareImage(width, height, ImageFormat::RGBA8_SRGB, ImageUsage::SampledImage);
}

void BaselineRayDisplay::present(std::span<const uint32_t> pixels, int width, int height) {
    // The compute device and Horizon presenter can be on different adapters.
    // Wait before reusing the upload buffer that the previous frame still reads.
    copy_.waitForDeferredResources();
    if (pixels.size() != static_cast<size_t>(width) * height || !staging_.copyFromData(pixels.data(), pixels.size_bytes()))
        throw std::runtime_error("Ray image upload failed");
    copy_ << staging_.copyTo(image_);
    const bool shown = surface_.present(copy_, image_);

    ++frame_;
    if (shown && remaining_frames_ > 0 && --remaining_frames_ == 0)
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    if (!glfwWindowShouldClose(window_)) return;
    const char* path = std::getenv("HORIZON_CAPTURE_PATH");
    if (!path || !*path) return;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    if (!stbi_write_png(path, width, height, 4, pixels.data(), width * 4))
        throw std::runtime_error("Could not write ray-traced PNG");
}

static ocarina::Device open_device() {
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
    auto& context = ocarina::RHIContext::instance(executable.parent_path());
    auto device = context.create_device();
    if (!device.supports_ray_queries())
        throw std::runtime_error("Ocarina ray queries unsupported on this device");
    device.init_rtx();
    return device;
}
