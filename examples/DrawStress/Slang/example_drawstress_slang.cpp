#include "Horizon.h"
#include "Codegen/ControlFlows.h"

#include SLANG(drawstress_vert.slang)
#include SLANG(drawstress_frag.slang)

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
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
    constexpr auto mode = "drawstress_slang";
    const auto env_int = [](const char* name, int fallback) {
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
    const bool hidden = env_int("HORIZON_BENCH_HIDDEN", 0) != 0;
    const bool offscreen = env_int("HORIZON_BENCH_OFFSCREEN", 0) != 0;
    int remaining_frames = env_int("HORIZON_PRESENT_FRAMES", 0);
    float fixed_dt = 0.0f;
    if (const char* value = std::getenv("HORIZON_FIXED_DT"); value && *value) {
        fixed_dt = std::stof(value);
        if (!std::isfinite(fixed_dt) || fixed_dt < 0.0f)
            throw std::invalid_argument("Invalid HORIZON_FIXED_DT");
    }

    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    if (hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    const auto close_window = [](GLFWwindow* window) {
        glfwDestroyWindow(window);
        glfwTerminate();
    };
    std::unique_ptr<GLFWwindow, decltype(close_window)> window(
        glfwCreateWindow(1920, 1080, mode, nullptr, nullptr), close_window);
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
    glm::vec4 axis_projection{};
    std::array<glm::vec4, dim_max> translations;

    RasterizerPipeline<drawstress_vert_slang_t, drawstress_frag_slang_t> rasterizer;

    HardwareImage final_output_image;
    HardwareDisplayer display(native_window, PresentDevicePreference::RenderDevice);
    HardwareExecutor render_executor;
    int width = 0, height = 0;

    double start = 0.0;
    uint64_t frame = 0;
    while (!glfwWindowShouldClose(window.get())) {
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
            final_output_image = HardwareImage(width, height, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
            final_output_image.setClearColor(0.19f, 0.19f, 0.19f, 1.0f);
            rasterizer.outColor = final_output_image;
            glm::mat4 projection = glm::perspectiveLH(glm::radians(60.0f), static_cast<float>(width) / height, 0.1f, 100.0f);
            projection[1][1] *= -1.0f; // Vulkan clip-space Y.
            const glm::mat4 view_projection = projection * view;
            axis_projection = {view_projection[0].x, view_projection[1].y, view_projection[2].z, view_projection[2].w};
            constexpr float step = 0.6f;
            const glm::vec3 base(-step * dim / 2.0f, -step * dim / 2.0f, -15.0f);
            for (int i = 0; i < dim; ++i)
                translations[i] = view_projection * glm::vec4(base + glm::vec3(i * step), 1.0f);
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
                std::array<glm::vec3, dim_max> sine, cosine;
                for (int i = 0; i < dim; ++i) {
                    const glm::vec3 angles = elapsed_seconds + float(i) * glm::vec3(0.21f, 0.37f, 0.13f);
                    sine[i] = glm::sin(angles);
                    cosine[i] = glm::cos(angles);
                }
                auto* dst = destination.data();
                for (int zz = 0; zz < dim; ++zz) {
                    const float sz = sine[zz].z, cz = cosine[zz].z;
                    for (int yy = 0; yy < dim; ++yy) {
                        const float sy = sine[yy].y, cy = cosine[yy].y;
                        const glm::vec4 axisX = projectAxis(cz * cy, cy * sz, -sy);
                        glm::vec4 translation = translations[zz];
                        translation.y = translations[yy].y;
                        for (int xx = 0; xx < dim; ++xx) {
                            const float sx = sine[xx].x, cx = cosine[xx].x;
                            translation.x = translations[xx].x;
                            const glm::mat4 matrix(axisX,
                                projectAxis(cz * sy * sx - cx * sz, cz * cx + sz * sy * sx, cy * sx),
                                projectAxis(sz * sx + cz * cx * sy, cx * sz * sy - cz * sx, cy * cx),
                                translation);
                            std::memcpy(dst, &matrix, sizeof(matrix));
                            dst += sizeof(matrix);
                        }
                    }
                }
            }, IndirectFirstInstance::AlwaysZero);

        render_executor << std::move(batch);
        if (offscreen) {
            render_executor.commit();
        } else if (!display.present(render_executor, final_output_image)) {
            continue;
        }
        if (remaining_frames > 0 && --remaining_frames == 0) break;
    }

    if (const char* filename = std::getenv("HORIZON_CAPTURE_PATH"); filename && *filename && final_output_image) {
        const uint32_t byte_count = width * height * 8;
        HardwareBuffer readback(byte_count, BufferUsage::StorageBuffer);
        render_executor << final_output_image.copyTo(readback);
        render_executor.commit().waitForDeferredResources();
        std::vector<uint16_t> half(static_cast<size_t>(width) * height * 4);
        if (!readback.copyToData(half.data(), byte_count)) throw std::runtime_error("Capture readback failed");
        std::vector<unsigned char> rgba(half.size());
        for (size_t i = 0; i < half.size(); ++i)
            rgba[i] = static_cast<unsigned char>(std::clamp(glm::unpackHalf1x16(half[i]), 0.0f, 1.0f) * 255.0f + 0.5f);
        const std::filesystem::path path(filename);
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        if (!stbi_write_png(filename, width, height, 4, rgba.data(), width * 4))
            throw std::runtime_error("Capture PNG write failed");
    } else {
        render_executor.waitForDeferredResources();
    }
}
