#include "Horizon.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#undef STBI_ONLY_PNG
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_STATIC
#include <tiny_obj_loader.h>

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include SLANG(baseline_vert.slang)
#include SLANG(baseline_frag.slang)

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

constexpr char window_title[] = "Horizon Baseline [Slang]";
const auto assets = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";

static int env_int(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    char* end{};
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end || parsed < 0 || parsed > 10000000)
        throw std::invalid_argument(std::string("Invalid ") + name);
    return static_cast<int>(parsed);
}

static RasterGeometry load_model();

static auto open_window(int width, int height, const char* title) {
    const bool hidden = env_int("HORIZON_BENCH_HIDDEN", 0) != 0;
    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    if (hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }
    glfwSetWindowSizeLimits(window, width, height, GLFW_DONT_CARE, GLFW_DONT_CARE);
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

void run_example_baseline_slang()
{
    constexpr int width = 1920, height = 1080;
    auto window = open_window(width, height, window_title);

    auto geometry = load_model();
    HardwareImage texture_image;
    {
        const auto filename = (assets / "textures/viking_room.png").string();
        int texture_width, texture_height;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load(filename.c_str(), &texture_width, &texture_height, nullptr, STBI_rgb_alpha), stbi_image_free);
        if (!pixels)
            throw std::runtime_error("Failed to load texture " + filename + ": " + stbi_failure_reason());

        HardwareImageCreateInfo texture_info(texture_width, texture_height, ImageFormat::RGBA8_SRGB);
        texture_info.linearFiltering = true;
        texture_image = HardwareImage(texture_info);
        const auto byte_count = static_cast<uint32_t>(texture_width) * static_cast<uint32_t>(texture_height) * 4u;
        HardwareBuffer staging(byte_count, BufferUsage::StorageBuffer, pixels.get());
        HardwareExecutor upload;
        upload << staging.copyTo(texture_image);
        upload.commit().waitForDeferredResources();
    }

    HardwareImageCreateInfo color_info(width, height, ImageFormat::RGBA16_FLOAT);
    color_info.usage = ImageUsage::SampledStorageImage;
    color_info.linearFiltering = true;
    HardwareImage color(color_info);
    color.setClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    const auto camera_view = glm::lookAt(glm::vec3(2.0f), glm::vec3(0.0f), glm::vec3(0, 0, 1));
    auto projection = glm::perspective(glm::radians(45.0f), float(width) / height, 0.1f, 10.0f);
    projection[1][1] *= -1.0f; // Vulkan clip-space Y.

    HardwareExecutor render_executor;
    HardwareDisplayer display(native_handle(window.get()), PresentDevicePreference::RenderDevice);

    RasterizerPipeline<baseline_vert_slang_t, baseline_frag_slang_t> rasterizer;
    rasterizer.setRasterState({.compare = RasterCompare::LessEqual});
    rasterizer.outColor = color;
    // 纹理存入 bindless combined-texture 表（set 0），拿到索引后经逐 draw 参数传入 shader。
    struct DrawParameters {
        glm::mat4 model{};
        uint32_t texIndex{};
    } parameters{};
    parameters.texIndex = texture_image.storeDescriptor();

    rasterizer.vp.view = camera_view;
    rasterizer.vp.proj = projection;

    const double start = glfwGetTime();
    int frames_remaining = env_int("HORIZON_PRESENT_FRAMES", 0);
    while (!glfwWindowShouldClose(window.get()))
    {
        glfwPollEvents();
        const float seconds = static_cast<float>(glfwGetTime() - start);
        const auto rotation = glm::rotate(glm::mat4(1.0f), seconds * glm::radians(90.0f), glm::vec3(0, 0, 1));

        parameters.model = rotation;

        auto batch = rasterizer.beginBatch();
        batch.draw(geometry, {}, parameters);
        render_executor << std::move(batch);
        const bool presented = display.present(render_executor, color);
        if (presented && frames_remaining > 0 && --frames_remaining == 0) break;
    }
    render_executor.waitForDeferredResources();
}

static RasterGeometry load_model()
{
    struct Vertex {
        std::array<float, 3> pos{};
        std::array<float, 2> tex_coord{};
        bool operator==(const Vertex&) const = default;
    };
    static_assert(sizeof(Vertex) == 5 * sizeof(float));

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
    auto hash_vertex = [](const Vertex& vertex) {
        size_t seed = 0;
        auto combine = [&seed](float value) {
            seed ^= std::hash<float>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        };
        for (float value : vertex.pos) combine(value);
        for (float value : vertex.tex_coord) combine(value);
        return seed;
    };
    std::unordered_map<Vertex, uint32_t, decltype(hash_vertex)> unique_vertices(0, hash_vertex);
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

    return RasterGeometry::upload(vertices, indices);
}
