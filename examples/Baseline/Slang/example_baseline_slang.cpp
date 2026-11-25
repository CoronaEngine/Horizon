#include "Horizon.h"

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
#endif
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include SLANG(baseline_vert.slang)
#include SLANG(baseline_frag.slang)

#include <array>
#include <cstdint>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <memory>
#include <utility>
#include <vector>

void run_example_baseline_slang()
{
    constexpr bool hidden = false;
    constexpr float fixed_dt = 0.0f;

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
    GLFWwindow* window = glfwCreateWindow(width, height, "Horizon Baseline [Slang]", nullptr, nullptr);

#if defined(_WIN32)
    void* native_window = glfwGetWin32Window(window);
#elif defined(__linux__)
    void* native_window = reinterpret_cast<void*>(glfwGetX11Window(window));
#elif defined(__APPLE__)
    void* native_window = glfwGetCocoaView(window);
#endif
    HardwareDisplayer display(native_window, PresentDevicePreference::RenderDevice);

    const auto assets = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";
    RasterGeometry geometry;
    {
        const auto filename = (assets / "models/viking_room.obj").string();
        const auto material_dir = (assets / "models/").string();
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        tinyobj::LoadObj(&attrib, &shapes, &materials, nullptr, nullptr, filename.c_str(), material_dir.c_str());

        using Vertex = std::array<float, 5>; // Position (x, y, z), then texture coordinates (u, v).
        std::map<Vertex, uint32_t> unique_vertices;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
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
                if (inserted) vertices.push_back(vertex);
                indices.push_back(found->second);
            }
        }
        geometry = RasterGeometry::upload(vertices, indices);
    }

    HardwareExecutor render_executor;
    HardwareImage texture_image;
    {
        const auto filename = (assets / "textures/viking_room.png").string();
        int texture_width = 0, texture_height = 0;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load(filename.c_str(), &texture_width, &texture_height, nullptr, STBI_rgb_alpha), stbi_image_free);

        HardwareImageCreateInfo texture_info(texture_width, texture_height, ImageFormat::RGBA8_SRGB);
        texture_info.linearFiltering = true;
        texture_image = HardwareImage(texture_info);
        render_executor << texture_image.copyFrom(pixels.get());
        render_executor.commit().waitForDeferredResources();
    }

    HardwareImage color(width, height, ImageFormat::RGBA16_FLOAT);
    RasterizerPipeline<baseline_vert_slang_t, baseline_frag_slang_t> rasterizer;
    rasterizer.setRasterState({.compare = RasterCompare::LessEqual});
    rasterizer.outColor = color;
    // 纹理存入 bindless combined-texture 表（set 0），拿到索引后经逐 draw 参数传入 shader。
    struct DrawParameters {
        glm::mat4 model{};
        uint32_t texIndex{};
    } parameters{};
    parameters.texIndex = texture_image.storeDescriptor();

    rasterizer.vp.view = glm::lookAt(glm::vec3(2.0f), glm::vec3(0.0f), glm::vec3(0, 0, 1));
    auto projection = glm::perspectiveRH_ZO(glm::radians(45.0f), float(width) / height, 0.1f, 10.0f);
    projection[1][1] *= -1.0f; // Vulkan clip-space Y.
    rasterizer.vp.proj = projection;

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
        parameters.model = glm::rotate(glm::mat4(1.0f), seconds * glm::radians(90.0f), glm::vec3(0, 0, 1));

        auto batch = rasterizer.beginBatch();
        batch.draw(geometry, parameters);
        render_executor << std::move(batch);
        display.present(render_executor, color);
        ++frame;
    }
    render_executor.waitForDeferredResources();
}
