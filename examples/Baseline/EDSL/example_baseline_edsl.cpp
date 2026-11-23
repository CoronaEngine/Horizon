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

#include "Codegen/BuiltinVariate.h"
#include "Codegen/TypeAlias.h"

#include <array>
#include <bit>
#include <cmath>
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

struct BaselineEdslVertexProxy {
    EmbeddedShader::Float3 pos;
    EmbeddedShader::Float2 tex_coord;
};

void run_example_baseline_edsl()
{
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
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    if (hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    const auto destroy_window = [](GLFWwindow* handle) {
        glfwDestroyWindow(handle);
        glfwTerminate();
    };
    std::unique_ptr<GLFWwindow, decltype(destroy_window)> window(
        glfwCreateWindow(1920, 1080, "Horizon Baseline [EDSL]", nullptr, nullptr), destroy_window);
    if (!window) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }
    void* native_window = nullptr;
#if defined(_WIN32)
    native_window = glfwGetWin32Window(window.get());
#elif defined(__APPLE__)
    native_window = glfwGetCocoaView(window.get());
#else
    throw std::runtime_error("Native Horizon window handles are unavailable on this platform");
#endif

    const auto assets = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";
    RasterGeometry geometry;
    {
        const auto model_path = assets / "models/viking_room.obj";
        const auto filename = model_path.string();
        const auto material_dir = (model_path.parent_path() / "").string();
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;
        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str(), material_dir.c_str()))
            throw std::runtime_error(warn + err);
        if (!warn.empty()) std::cerr << warn << '\n';

        using Vertex = std::array<float, 5>; // Position (x, y, z), then texture coordinates (u, v).
        auto hash_vertex = [](const Vertex& vertex) {
            size_t seed = 0;
            for (float value : vertex)
                seed ^= std::hash<float>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        };
        std::unordered_map<Vertex, uint32_t, decltype(hash_vertex)> unique_vertices;
        std::vector<Vertex> vertices;
        std::vector<uint16_t> indices;
        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                const size_t p = 3 * static_cast<size_t>(index.vertex_index);
                Vertex vertex{attrib.vertices[p], attrib.vertices[p + 1], attrib.vertices[p + 2]};
                if (index.texcoord_index >= 0) {
                    const size_t uv = 2 * static_cast<size_t>(index.texcoord_index);
                    vertex[3] = attrib.texcoords[uv];
                    vertex[4] = 1.0f - attrib.texcoords[uv + 1];
                }
                const auto [found, inserted] = unique_vertices.emplace(vertex, static_cast<uint32_t>(vertices.size()));
                if (inserted) {
                    if (vertices.size() >= 0x10000u)
                        throw std::runtime_error("Mesh exceeds the 65536-vertex 16-bit index limit: " + filename);
                    vertices.push_back(vertex);
                }
                indices.push_back(static_cast<uint16_t>(found->second));
            }
        }
        geometry = RasterGeometry::upload(vertices, indices);
    }

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
        HardwareExecutor upload;
        upload << texture_image.copyFrom(pixels.get());
        upload.commit().waitForDeferredResources();
    }

    HardwareImage color;
    int width = 0, height = 0;
    HardwareExecutor render_executor;
    HardwareDisplayer display(native_window, PresentDevicePreference::RenderDevice);

    using EmbeddedShader::Float2, EmbeddedShader::Float4;
    using EmbeddedShader::mul;

    EmbeddedShader::Texture2D<ktm::fvec4> texture_proxy = texture_image;
    EmbeddedShader::Texture2D<ktm::fvec4> final_output_proxy;
    EmbeddedShader::Float4x4 model;
    EmbeddedShader::Float4x4 view;
    EmbeddedShader::Float4x4 proj;

    auto vertex_shader = [&](EmbeddedShader::Aggregate<BaselineEdslVertexProxy> vertex) -> Float2 {
        EmbeddedShader::position() = mul(proj, mul(view, mul(model, Float4(vertex->pos, 1.0f))));
        return vertex->tex_coord;
    };

    auto fragment_shader = [&](Float2 uv) {
        final_output_proxy << EmbeddedShader::texture(texture_proxy, uv);
    };

    RasterizerPipeline<> rasterizer(vertex_shader, fragment_shader, {.enableMatrixColumnMajor = true});
    rasterizer.setRasterState({.compare = RasterCompare::LessEqual});

    view = std::bit_cast<ktm::fmat4x4>(glm::lookAt(glm::vec3(2.0f), glm::vec3(0.0f), glm::vec3(0, 0, 1)));

    double start = 0.0;
    uint64_t frame = 0;
    while (!glfwWindowShouldClose(window.get()))
    {
        glfwPollEvents();
        int w, h;
        glfwGetFramebufferSize(window.get(), &w, &h);
        if (w == 0 || h == 0) {
            if (!glfwWindowShouldClose(window.get())) glfwWaitEvents();
            continue;
        }
        if (w != width || h != height) {
            render_executor.waitForDeferredResources();
            width = w;
            height = h;
            color = HardwareImage(width, height, ImageFormat::RGBA16_FLOAT, ImageUsage::SampledStorageImage);
            final_output_proxy = color;
            auto projection = glm::perspective(glm::radians(45.0f), float(width) / height, 0.1f, 10.0f);
            projection[1][1] *= -1.0f; // Vulkan clip-space Y.
            proj = std::bit_cast<ktm::fmat4x4>(projection);
        }
        if (frame == 0) start = glfwGetTime();
        const float seconds = fixed_dt ? static_cast<float>(frame) * fixed_dt : static_cast<float>(glfwGetTime() - start);
        model = std::bit_cast<ktm::fmat4x4>(
            glm::rotate(glm::mat4(1.0f), seconds * glm::radians(90.0f), glm::vec3(0, 0, 1)));

        auto batch = rasterizer.beginBatch();
        batch.draw(geometry);
        render_executor << std::move(batch);
        const bool presented = display.present(render_executor, color);
        ++frame;
        if (presented && frames_remaining > 0 && --frames_remaining == 0) break;
    }
    render_executor.waitForDeferredResources();
}
