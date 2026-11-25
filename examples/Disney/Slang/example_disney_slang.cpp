#include "Horizon.h"
#include "Codegen/ControlFlows.h"

#include SLANG(disney_vert.slang)
#include SLANG(disney_frag.slang)

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
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <utility>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#undef STB_IMAGE_WRITE_IMPLEMENTATION
#undef STB_IMAGE_WRITE_STATIC

void run_example_disney_slang()
{
    constexpr bool hidden = false;
    constexpr bool offscreen = false;
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
    GLFWwindow* window = glfwCreateWindow(width, height, "disney_slang", nullptr, nullptr);

#if defined(_WIN32)
    void* native_window = glfwGetWin32Window(window);
#elif defined(__linux__)
    void* native_window = reinterpret_cast<void*>(glfwGetX11Window(window));
#elif defined(__APPLE__)
    void* native_window = glfwGetCocoaView(window);
#endif
    HardwareDisplayer display(native_window, PresentDevicePreference::RenderDevice);

    // Upload the six faces and all prefiltered mips of each environment map.
    const auto environment_dir = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "env";
    HardwareExecutor render_executor;
    HardwareImage radiance, irradiance;
    for (const auto& [filename, face_size, mip_count, cubemap] : {
            std::tuple{"bolonga_lod.dds", 256u, 9u, &radiance},
            std::tuple{"bolonga_irr.dds", 128u, 1u, &irradiance}}) {
        uint32_t texels_per_face = 0;
        for (uint32_t mip = 0; mip < mip_count; ++mip) {
            const uint32_t dim = face_size >> mip;
            texels_per_face += dim * dim;
        }
        std::vector<uint64_t> texels(6 * texels_per_face);
        std::ifstream file(environment_dir / filename, std::ios::binary);
        file.seekg(148); // The bundled DX10 DDS files store RGBA16F texels after the header.
        file.read(reinterpret_cast<char*>(texels.data()), texels.size() * sizeof(uint64_t));

        HardwareImageCreateInfo desc(face_size, face_size, ImageFormat::RGBA16_FLOAT);
        desc.arrayLayers = 6;
        desc.cube = true;
        desc.linearFiltering = true;
        desc.mipLevels = static_cast<int>(mip_count);
        *cubemap = HardwareImage(desc);
        HardwareBuffer staging(texels, BufferUsage::StorageBuffer);
        uint64_t offset = 0;
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t mip = 0; mip < mip_count; ++mip) {
                const uint32_t dim = face_size >> mip;
                render_executor << staging.copyTo(*cubemap, offset, face, mip);
                offset += static_cast<uint64_t>(dim) * dim * sizeof(uint64_t);
            }
        }
    }
    render_executor.commit().waitForDeferredResources();

    // Each triangle-strip vertex also supplies the sphere's object-space normal.
    RasterGeometry sphere;
    {
        constexpr int slices = 48, stacks = 24;
        std::vector<std::array<float, 3>> vertices;
        std::vector<uint16_t> indices;
        vertices.reserve(static_cast<size_t>((slices + 1) * (stacks + 1)));
        indices.reserve(static_cast<size_t>(stacks * (slices + 1) * 2 + (stacks - 1) * 2));

        for (int y = 0; y <= stacks; ++y) {
            const float phi = (static_cast<float>(y) / stacks) * glm::pi<float>();
            const float sin_phi = std::sin(phi);
            const float cos_phi = std::cos(phi);

            for (int x = 0; x <= slices; ++x) {
                const float theta = (static_cast<float>(x) / slices) * glm::two_pi<float>();
                vertices.push_back({ sin_phi * std::cos(theta), cos_phi, sin_phi * std::sin(theta) });
            }
        }

        for (int y = 0; y < stacks; ++y) {
            const auto first = static_cast<uint16_t>(y * (slices + 1));
            if (y != 0) {
                indices.push_back(indices.back());
                indices.push_back(first);
            }
            for (int x = 0; x <= slices; ++x) {
                indices.push_back(static_cast<uint16_t>(first + x));
                indices.push_back(static_cast<uint16_t>(first + slices + 1 + x));
            }
        }

        sphere = RasterGeometry::upload(vertices, indices);
    }

    struct DrawParameters {
        glm::mat4 model{1.0f};
        glm::vec4 baseColorMetallic;
        float roughness = 0.5f, specular = 0.5f, specularTint = 0.0f, subsurface = 0.0f;
        float anisotropic = 0.0f, sheen = 0.0f, sheenTint = 0.5f, clearcoat = 0.0f;
        float clearcoatGloss = 1.0f;
    };

    constexpr int rows = 10, columns = 11;
    constexpr float radius = 0.42f, spacing = 1.12f;
    std::array<DrawParameters, rows * columns> draws;
    for (int col = 0; col < columns; ++col) {
        const float x = (col - (columns - 1) * 0.5f) * spacing;
        const float t = static_cast<float>(col) / (columns - 1);
        const DrawParameters column[rows] = {
            {.baseColorMetallic = {1.00f, 1.00f, 1.00f, 0.f}, .subsurface = t},
            {.baseColorMetallic = {1.00f, 0.86f, 0.12f, t}},
            {.baseColorMetallic = {0.92f, 0.12f, 0.12f, 0.f}, .specular = t},
            {.baseColorMetallic = {0.92f, 0.12f, 0.12f, 0.f}, .specularTint = t},
            {.baseColorMetallic = {0.42f, 0.32f, 0.92f, 0.f}, .roughness = t},
            {.baseColorMetallic = {0.92f, 0.18f, 0.78f, 0.f}, .anisotropic = t},
            {.baseColorMetallic = {0.62f, 0.18f, 0.14f, 0.f}, .sheen = t},
            {.baseColorMetallic = {0.62f, 0.18f, 0.14f, 0.f}, .sheenTint = t},
            {.baseColorMetallic = {0.10f, 0.34f, 0.40f, 0.f}, .clearcoat = t},
            {.baseColorMetallic = {0.10f, 0.34f, 0.40f, 0.f}, .clearcoat = 1.f, .clearcoatGloss = t},
        };
        for (int row = 0; row < rows; ++row) {
            const float y = ((rows - 1) * 0.5f - row) * spacing;
            auto& draw = draws[row * columns + col];
            draw = column[row];
            draw.model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f))
                       * glm::scale(glm::mat4(1.0f), glm::vec3(radius));
        }
    }

    HardwareImage final_output_image(width, height, ImageFormat::RGBA16_FLOAT);
    final_output_image.setClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    RasterizerPipeline<disney_vert_slang_t, disney_frag_slang_t> rasterizer;
    rasterizer.outColor = final_output_image;
    rasterizer.setRasterState({.topology = RasterTopology::TriangleStrip});
    rasterizer.uses({{radiance, ResourceAccess::Read}, {irradiance, ResourceAccess::Read}});

    constexpr float yaw = glm::pi<float>(), pitch = 0.42f * glm::pi<float>();
    const auto eye = 13.5f * glm::vec3(std::sin(pitch) * std::sin(yaw), std::cos(pitch), std::sin(pitch) * std::cos(yaw));
    const auto view = glm::lookAtLH(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    auto projection = glm::perspectiveLH_ZO(glm::radians(45.0f), static_cast<float>(width) / height, 0.1f, 100.0f);
    projection[1][1] *= -1.0f;
    rasterizer.vsp.proj_view = projection * view;
    rasterizer.vsp.camPos = glm::vec4(eye, 1.0f);
    rasterizer.vsp.lightDir = glm::vec4(glm::normalize(glm::vec3(-0.35f, 0.55f, -0.75f)), 0.0f);
    rasterizer.vsp.texCubeIndex = radiance;
    rasterizer.vsp.texCubeIrrIndex = irradiance;

    bool has_rendered_frame = false;
    for (;;) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) break;
        if (!offscreen && glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            glfwWaitEvents();
            continue;
        }
        auto batch = rasterizer.beginBatch();
        for (const auto& draw : draws)
            batch.draw(sphere, draw);

        render_executor << std::move(batch);
        has_rendered_frame = true;
        if (offscreen) {
            render_executor.commit();
        } else {
            display.present(render_executor, final_output_image);
        }
    }

    if (capture_path[0] && has_rendered_frame) {
        const std::filesystem::path path(capture_path);
        HardwareBuffer readback(height, width * 8u, BufferUsage::StorageBuffer);
        render_executor << final_output_image.copyTo(readback);
        render_executor.commit().waitForDeferredResources();
        std::vector<uint16_t> half(static_cast<size_t>(width) * height * 4);
        readback.copyToData(half);
        std::vector<unsigned char> rgba(half.size());
        for (size_t i = 0; i < half.size(); ++i)
            rgba[i] = static_cast<unsigned char>(std::clamp(glm::unpackHalf1x16(half[i]), 0.0f, 1.0f) * 255.0f + 0.5f);
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        stbi_write_png(capture_path, width, height, 4, rgba.data(), width * 4);
    } else {
        render_executor.waitForDeferredResources();
    }
}
