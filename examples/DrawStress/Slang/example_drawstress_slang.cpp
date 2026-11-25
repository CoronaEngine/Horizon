#include "Horizon.h"
#include "Codegen/ControlFlows.h"

#include SLANG(drawstress_vert.slang)
#include SLANG(drawstress_frag.slang)

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

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#undef STB_IMAGE_WRITE_IMPLEMENTATION
#undef STB_IMAGE_WRITE_STATIC

// Based on the single-threaded bgfx examples/17-drawstress workload.
void run_example_drawstress_slang()
{
    constexpr int dim = 40;
    constexpr bool hidden = false;
    constexpr bool offscreen = false;
    constexpr float fixed_dt = 0.0f;
    constexpr char capture_path[] = "";

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
    GLFWwindow* window = glfwCreateWindow(width, height, "drawstress_slang", nullptr, nullptr);

#if defined(_WIN32)
    void* native_window = glfwGetWin32Window(window);
#elif defined(__linux__)
    void* native_window = reinterpret_cast<void*>(glfwGetX11Window(window));
#elif defined(__APPLE__)
    void* native_window = glfwGetCocoaView(window);
#endif
    HardwareDisplayer display(native_window, PresentDevicePreference::RenderDevice);

    // The buffer supplies the vertex count. Both shaders decode the indexed corner directly.
    constexpr std::array<uint32_t, 8> cube_vertices = { 0, 1, 2, 3, 4, 5, 6, 7 };

    constexpr std::array<uint16_t, 36> cube_indices = {
        0, 1, 2, 1, 3, 2,
        4, 6, 5, 5, 6, 7,
        0, 2, 4, 4, 2, 6,
        1, 5, 3, 5, 7, 3,
        0, 4, 1, 4, 5, 1,
        2, 3, 6, 6, 3, 7,
    };
    const uint32_t draw_count = static_cast<uint32_t>(dim * dim * dim);
    auto cube = RasterGeometry::upload(cube_vertices, cube_indices);
    HardwareBuffer commands;
    {
        // Each packet points to its cube's mat4 in the per-frame payload, in 32-bit words.
        std::vector<RasterIndirectPacket> packets(draw_count);
        for (uint32_t i = 0; i < draw_count; ++i) {
            packets[i].vertexCount = static_cast<uint32_t>(cube_indices.size());
            packets[i].instanceCount = 1;
            packets[i].parameterWord = i * (sizeof(glm::mat4) / sizeof(uint32_t));
        }
        commands = HardwareBuffer::uploadDeviceLocal(packets, BufferUsage::RasterIndirect);
    }

    // Match the left-handed camera in the bgfx drawstress example.
    const glm::mat4 view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, -35.0f), glm::vec3(0.0f),
                                         glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspectiveLH_ZO(glm::radians(60.0f), static_cast<float>(width) / height, 0.1f, 100.0f);
    projection[1][1] *= -1.0f; // Vulkan clip-space Y.
    const glm::mat4 view_projection = projection * view;
    const glm::vec4 axis_projection{view_projection[0].x, view_projection[1].y, view_projection[2].z, view_projection[2].w};
    constexpr float step = 0.6f;
    const glm::vec3 base(-step * dim / 2.0f, -step * dim / 2.0f, -15.0f);
    std::array<glm::vec4, dim> translations;
    for (int i = 0; i < dim; ++i)
        translations[i] = view_projection * glm::vec4(base + glm::vec3(i * step), 1.0f);

    RasterizerPipeline<drawstress_vert_slang_t, drawstress_frag_slang_t> rasterizer;

    HardwareImage final_output_image(width, height, ImageFormat::RGBA16_FLOAT);
    final_output_image.setClearColor(0.19f, 0.19f, 0.19f, 1.0f);
    rasterizer.outColor = final_output_image;
    HardwareExecutor render_executor;

    double start = 0.0;
    uint64_t frame = 0;
    for (;;) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) break;
        if (!offscreen && glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            glfwWaitEvents();
            continue;
        }
        if (frame == 0) start = glfwGetTime();
        const float elapsed_seconds = fixed_dt ? static_cast<float>(frame) * fixed_dt : static_cast<float>(glfwGetTime() - start);
        ++frame;
        auto batch = rasterizer.beginBatch();
        batch.drawIndirect(cube, commands, draw_count, draw_count * sizeof(glm::mat4),
            [&](std::span<std::byte> destination) {
                // The fixed view faces +Z. Clip X and Y scale independently, and Z supplies both clip Z and W.
                const auto projectAxis = [&](float x, float y, float z) {
                    return axis_projection * (0.25f * glm::vec4(x, y, z, z));
                };

                // Each axis has only dim distinct angles. Keep GLM's eulerAngleZYX product order.
                std::array<glm::vec3, dim> sine, cosine;
                for (int i = 0; i < dim; ++i) {
                    const glm::vec3 angles = elapsed_seconds + float(i) * glm::vec3(0.21f, 0.37f, 0.13f);
                    sine[i] = glm::sin(angles);
                    cosine[i] = glm::cos(angles);
                }
                auto* matrix_bytes = destination.data();
                for (int z = 0; z < dim; ++z) {
                    const float sz = sine[z].z, cz = cosine[z].z;
                    for (int y = 0; y < dim; ++y) {
                        const float sy = sine[y].y, cy = cosine[y].y;
                        const glm::vec4 axisX = projectAxis(cz * cy, cy * sz, -sy);
                        glm::vec4 translation = translations[z];
                        translation.y = translations[y].y;
                        for (int x = 0; x < dim; ++x) {
                            const float sx = sine[x].x, cx = cosine[x].x;
                            translation.x = translations[x].x;
                            const glm::mat4 matrix(axisX,
                                projectAxis(cz * sy * sx - cx * sz, cz * cx + sz * sy * sx, cy * sx),
                                projectAxis(sz * sx + cz * cx * sy, cx * sz * sy - cz * sx, cy * cx),
                                translation);
                            std::memcpy(matrix_bytes, &matrix, sizeof(matrix));
                            matrix_bytes += sizeof(matrix);
                        }
                    }
                }
            }, IndirectFirstInstance::AlwaysZero);

        render_executor << std::move(batch);
        if (offscreen) {
            render_executor.commit();
        } else {
            display.present(render_executor, final_output_image);
        }
    }

    if (capture_path[0] != '\0' && frame > 0) {
        HardwareBuffer readback(height, width * 8u, BufferUsage::StorageBuffer);
        render_executor << final_output_image.copyTo(readback);
        render_executor.commit().waitForDeferredResources();
        std::vector<uint16_t> half(static_cast<size_t>(width) * height * 4);
        readback.copyToData(half);
        std::vector<unsigned char> rgba(half.size());
        for (size_t i = 0; i < half.size(); ++i)
            rgba[i] = static_cast<unsigned char>(std::clamp(glm::unpackHalf1x16(half[i]), 0.0f, 1.0f) * 255.0f + 0.5f);
        const std::filesystem::path path(capture_path);
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        stbi_write_png(capture_path, width, height, 4, rgba.data(), width * 4);
    } else {
        render_executor.waitForDeferredResources();
    }
}
