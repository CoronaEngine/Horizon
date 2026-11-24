#include "Horizon.h"
#include "Codegen/ControlFlows.h"

#include SLANG(disney_vert.slang)
#include SLANG(disney_frag.slang)

#include <GLFW/glfw3.h>
#include <stdexcept>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
static constexpr auto native_window_handle = glfwGetWin32Window;
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
static constexpr auto native_window_handle = glfwGetCocoaView;
#else
static void* native_window_handle(GLFWwindow*) {
    throw std::runtime_error("Native Horizon window handles are unavailable on this platform");
}
#endif
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#undef STB_IMAGE_WRITE_IMPLEMENTATION
#undef STB_IMAGE_WRITE_STATIC

void run_example_disney_slang()
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
    int remaining_frames = env_int("HORIZON_PRESENT_FRAMES");
    const bool offscreen = env_int("HORIZON_BENCH_OFFSCREEN") != 0;

    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    struct GlfwSession {
        ~GlfwSession() { glfwTerminate(); } // Also destroys the window.
    } glfw;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    if (hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(1920, 1080, "disney_slang", nullptr, nullptr);
    if (!window) throw std::runtime_error("glfwCreateWindow failed");

    HardwareDisplayer display(native_window_handle(window), PresentDevicePreference::RenderDevice);

    // Upload the six faces and all prefiltered mips of each environment map.
    const auto environment_dir = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "env";
    HardwareExecutor render_executor;
    HardwareImage radiance, irradiance;
    for (const auto& [filename, cubemap] : {std::pair{"bolonga_lod.dds", &radiance},
                                           std::pair{"bolonga_irr.dds", &irradiance}}) {
        const auto path = environment_dir / filename;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("Failed to open DDS file: " + path.string());

        const std::streamsize file_size = file.tellg();
        constexpr size_t dx10_payload_offset = 148;
        if (file_size < static_cast<std::streamsize>(dx10_payload_offset))
            throw std::runtime_error("Truncated DDS header: " + path.string());
        file.seekg(0);
        std::vector<std::byte> bytes(static_cast<size_t>(file_size));
        if (!file.read(reinterpret_cast<char*>(bytes.data()), file_size))
            throw std::runtime_error("Failed to read DDS: " + path.string());

        constexpr uint32_t dds_magic = 0x20534444;
        constexpr uint32_t fourcc_dx10 = 0x30315844;
        constexpr uint32_t dxgi_format_rgba16_float = 10;
        constexpr uint32_t caps2_cubemap = 0x200;

        const auto read_u32 = [&](size_t offset) {
            uint32_t value;
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        };
        if (read_u32(0) != dds_magic)
            throw std::runtime_error("Not a DDS file: " + path.string());

        const uint32_t height = read_u32(12);
        const uint32_t width = read_u32(16);
        const uint32_t mip_count = std::max(1u, read_u32(28));
        if (read_u32(84) != fourcc_dx10 || read_u32(128) != dxgi_format_rgba16_float)
            throw std::runtime_error("Expected DX10 RGBA16F DDS: " + path.string());
        if ((read_u32(112) & caps2_cubemap) == 0 || width != height)
            throw std::runtime_error("Expected cubemap DDS: " + path.string());

        HardwareImageCreateInfo desc(width, height, ImageFormat::RGBA16_FLOAT);
        desc.arrayLayers = 6;
        desc.cube = true;
        desc.linearFiltering = true;
        desc.mipLevels = static_cast<int>(mip_count);
        *cubemap = HardwareImage(desc);
        const size_t payload_size = bytes.size() - dx10_payload_offset;
        HardwareBuffer staging(static_cast<uint32_t>(payload_size), BufferUsage::StorageBuffer,
                               bytes.data() + dx10_payload_offset);
        uint64_t offset = 0;
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t mip = 0; mip < mip_count; ++mip) {
                const uint32_t dim = std::max(1u, width >> mip);
                const uint64_t mip_bytes = static_cast<uint64_t>(dim) * dim * 8;
                if (offset + mip_bytes > payload_size) throw std::runtime_error("Truncated DDS payload");
                render_executor << staging.copyTo(*cubemap, offset, face, mip);
                offset += mip_bytes;
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
    static_assert(offsetof(DrawParameters, baseColorMetallic) == 16 * sizeof(uint32_t));
    static_assert(offsetof(DrawParameters, roughness) == 20 * sizeof(uint32_t));
    static_assert(offsetof(DrawParameters, anisotropic) == 24 * sizeof(uint32_t));
    static_assert(offsetof(DrawParameters, clearcoatGloss) == 28 * sizeof(uint32_t));
    static_assert(sizeof(DrawParameters) == 29 * sizeof(uint32_t));

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

    HardwareImage final_output_image;
    RasterizerPipeline<disney_vert_slang_t, disney_frag_slang_t> rasterizer;
    rasterizer.setRasterState({.topology = RasterTopology::TriangleStrip});
    rasterizer.uses({{radiance, ResourceAccess::Read}, {irradiance, ResourceAccess::Read}});

    constexpr float yaw = glm::pi<float>(), pitch = 0.42f * glm::pi<float>();
    const auto eye = 13.5f * glm::vec3(std::sin(pitch) * std::sin(yaw), std::cos(pitch), std::sin(pitch) * std::cos(yaw));
    const auto view = glm::lookAtLH(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    rasterizer.vsp.camPos = glm::vec4(eye, 1.0f);
    rasterizer.vsp.lightDir = glm::vec4(glm::normalize(glm::vec3(-0.35f, 0.55f, -0.75f)), 0.0f);
    rasterizer.vsp.texCubeIndex = radiance;
    rasterizer.vsp.texCubeIrrIndex = irradiance;

    int width = 0, height = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        if (w == 0 || h == 0) {
            if (!glfwWindowShouldClose(window)) glfwWaitEvents();
            continue;
        }
        if (w != width || h != height) {
            render_executor.waitForDeferredResources();
            width = w;
            height = h;
            final_output_image = HardwareImage(width, height, ImageFormat::RGBA16_FLOAT);
            final_output_image.setClearColor(0.5f, 0.5f, 0.5f, 1.0f);
            rasterizer.outColor = final_output_image;
            auto projection = glm::perspectiveLH_ZO(glm::radians(45.0f), static_cast<float>(width) / height, 0.1f, 100.0f);
            projection[1][1] *= -1.0f;
            rasterizer.vsp.proj_view = projection * view;
        }
        auto batch = rasterizer.beginBatch();
        for (const auto& draw : draws)
            batch.draw(sphere, draw);

        render_executor << std::move(batch);
        if (offscreen) {
            render_executor.commit();
        } else if (!display.present(render_executor, final_output_image)) {
            continue;
        }
        if (remaining_frames > 0 && --remaining_frames == 0) break;
    }

    if (const char* filename = std::getenv("HORIZON_CAPTURE_PATH"); filename && *filename && final_output_image) {
        const std::filesystem::path path(filename);
        HardwareBuffer readback(height, width * 8u, BufferUsage::StorageBuffer);
        render_executor << final_output_image.copyTo(readback);
        render_executor.commit().waitForDeferredResources();
        std::vector<uint16_t> half(static_cast<size_t>(width) * height * 4);
        if (!readback.copyToData(half)) throw std::runtime_error("Capture readback failed");
        std::vector<unsigned char> rgba(half.size());
        for (size_t i = 0; i < half.size(); ++i)
            rgba[i] = static_cast<unsigned char>(std::clamp(glm::unpackHalf1x16(half[i]), 0.0f, 1.0f) * 255.0f + 0.5f);
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        if (!stbi_write_png(filename, width, height, 4, rgba.data(), width * 4))
            throw std::runtime_error("Capture PNG write failed");
    } else {
        render_executor.waitForDeferredResources();
    }
}
