#include "Horizon.h"
#include "Codegen/ControlFlows.h"

#include SLANG(disney_vert.slang)
#include SLANG(disney_frag.slang)

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
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
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
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
    const auto close = [](GLFWwindow* handle) {
        glfwDestroyWindow(handle);
        glfwTerminate();
    };
    std::unique_ptr<GLFWwindow, decltype(close)> window(
        glfwCreateWindow(1920, 1080, "disney_slang", nullptr, nullptr), close);
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

    // Upload the six faces and all prefiltered mips of each environment map.
    const auto environment_dir = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "env";
    HardwareImage radiance, irradiance;
    for (const auto& [filename, cubemap] : {std::pair{"bolonga_lod.dds", &radiance},
                                           std::pair{"bolonga_irr.dds", &irradiance}}) {
        const auto path = environment_dir / filename;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            throw std::runtime_error("Failed to open DDS file: " + path.string());

        const std::streamsize file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<std::byte> bytes(static_cast<size_t>(file_size));
        file.read(reinterpret_cast<char*>(bytes.data()), file_size);

        constexpr uint32_t dds_magic = 0x20534444;
        constexpr uint32_t fourcc_dx10 = 0x30315844;
        constexpr uint32_t dxgi_format_rgba16_float = 10;
        constexpr uint32_t caps2_cubemap = 0x200;
        constexpr size_t dx10_payload_offset = 148;

        const auto read_u32 = [&](size_t offset) {
            uint32_t value;
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        };
        if (bytes.size() < dx10_payload_offset || read_u32(0) != dds_magic)
            throw std::runtime_error("Not a DDS file: " + path.string());

        const uint32_t height = read_u32(12);
        const uint32_t width = read_u32(16);
        const uint32_t mip_count = std::max(1u, read_u32(28));
        const uint32_t fourcc = read_u32(84);
        const uint32_t caps2 = read_u32(112);

        if (fourcc != fourcc_dx10 || read_u32(128) != dxgi_format_rgba16_float)
            throw std::runtime_error("Expected DX10 RGBA16F DDS: " + path.string());
        if ((caps2 & caps2_cubemap) == 0 || width != height)
            throw std::runtime_error("Expected cubemap DDS: " + path.string());

        HardwareImageCreateInfo desc(width, height, ImageFormat::RGBA16_FLOAT);
        desc.arrayLayers = 6;
        desc.cube = true;
        desc.linearFiltering = true;
        desc.mipLevels = static_cast<int>(mip_count);
        desc.usage = ImageUsage::SampledImage;
        *cubemap = HardwareImage(desc);
        const size_t payload_size = bytes.size() - dx10_payload_offset;
        HardwareBuffer staging(static_cast<uint32_t>(payload_size), BufferUsage::StorageBuffer,
                               bytes.data() + dx10_payload_offset);
        HardwareExecutor executor;
        uint64_t offset = 0;
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t mip = 0; mip < mip_count; ++mip) {
                const uint32_t dim = std::max(1u, width >> mip);
                const uint64_t mip_bytes = static_cast<uint64_t>(dim) * dim * 8;
                if (offset + mip_bytes > payload_size) throw std::runtime_error("Truncated DDS payload");
                executor << staging.copyTo(*cubemap, offset, face, mip);
                offset += mip_bytes;
            }
        }
        executor.commit().waitForDeferredResources();
    }

    // Each triangle-strip vertex also supplies the sphere's object-space normal.
    RasterGeometry sphere;
    {
        constexpr int slices = 48, stacks = 24;
        std::vector<std::array<float, 3>> vertices;
        std::vector<uint16_t> indices;
        vertices.reserve(static_cast<size_t>((slices + 1) * (stacks + 1)));
        indices.reserve(static_cast<size_t>(stacks * (slices + 1) * 2 + (stacks - 1) * 2));

        for (int y = 0; y <= stacks; ++y) {
            const float v = static_cast<float>(y) / static_cast<float>(stacks);
            const float phi = v * glm::pi<float>();
            const float sin_phi = std::sin(phi);
            const float cos_phi = std::cos(phi);

            for (int x = 0; x <= slices; ++x) {
                const float u = static_cast<float>(x) / static_cast<float>(slices);
                const float theta = u * glm::two_pi<float>();
                const float sin_theta = std::sin(theta);
                const float cos_theta = std::cos(theta);

                vertices.push_back({ sin_phi * cos_theta, cos_phi, sin_phi * sin_theta });
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
        glm::mat4 model;
        glm::vec4 baseColorMetallic;
        float roughness = 0.5f, specular = 0.5f, specularTint = 0.0f, subsurface = 0.0f;
        float anisotropic = 0.0f, sheen = 0.0f, sheenTint = 0.5f, clearcoat = 0.0f;
        float clearcoatGloss = 1.0f;
        uint32_t texCubeIndex, texCubeIrrIndex;
    };
    static_assert(offsetof(DrawParameters, baseColorMetallic) == 16 * sizeof(uint32_t));
    static_assert(offsetof(DrawParameters, roughness) == 20 * sizeof(uint32_t));
    static_assert(offsetof(DrawParameters, anisotropic) == 24 * sizeof(uint32_t));
    static_assert(offsetof(DrawParameters, clearcoatGloss) == 28 * sizeof(uint32_t));
    static_assert(offsetof(DrawParameters, texCubeIndex) == 29 * sizeof(uint32_t));
    static_assert(offsetof(DrawParameters, texCubeIrrIndex) == 30 * sizeof(uint32_t));
    const uint32_t cube_index = radiance.storeDescriptor();
    const uint32_t irradiance_index = irradiance.storeDescriptor();

    constexpr int chart_row_count = 10, chart_col_count = 11;
    constexpr glm::vec3 chart_row_colors[chart_row_count] = {
        glm::vec3(1.0f, 1.0f, 1.0f),       // subsurface
        glm::vec3(1.0f, 0.86f, 0.12f),     // metallic
        glm::vec3(0.92f, 0.12f, 0.12f),    // specular
        glm::vec3(0.92f, 0.12f, 0.12f),    // specularTint
        glm::vec3(0.42f, 0.32f, 0.92f),    // roughness
        glm::vec3(0.92f, 0.18f, 0.78f),    // anisotropic
        glm::vec3(0.62f, 0.18f, 0.14f),    // sheen
        glm::vec3(0.62f, 0.18f, 0.14f),    // sheenTint
        glm::vec3(0.10f, 0.34f, 0.40f),    // clearcoat
        glm::vec3(0.10f, 0.34f, 0.40f),    // clearcoatGloss
    };
    constexpr float sphere_scale = 0.42f, spacing = 1.12f;
    std::array<DrawParameters, chart_row_count * chart_col_count> draws{};
    for (int row = 0; row < chart_row_count; ++row) {
        const float y = ((chart_row_count - 1) * 0.5f - static_cast<float>(row)) * spacing;
        for (int col = 0; col < chart_col_count; ++col) {
            const float x = (static_cast<float>(col) - (chart_col_count - 1) * 0.5f) * spacing;
            const float t = static_cast<float>(col) / static_cast<float>(chart_col_count - 1);
            auto& draw = draws[row * chart_col_count + col];
            draw.model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f))
                       * glm::scale(glm::mat4(1.0f), glm::vec3(sphere_scale));
            draw.baseColorMetallic = glm::vec4(chart_row_colors[row], 0.0f);
            draw.texCubeIndex = cube_index;
            draw.texCubeIrrIndex = irradiance_index;
            switch (row) {
            case 0: draw.subsurface = t; break;
            case 1: draw.baseColorMetallic.w = t; break;
            case 2: draw.specular = t; break;
            case 3: draw.specularTint = t; break;
            case 4: draw.roughness = t; break;
            case 5: draw.anisotropic = t; break;
            case 6: draw.sheen = t; break;
            case 7: draw.sheenTint = t; break;
            case 8: draw.clearcoat = t; break;
            case 9:
                draw.clearcoat = 1.0f;
                draw.clearcoatGloss = t;
                break;
            }
        }
    }

    HardwareImage final_output_image;
    RasterizerPipeline<disney_vert_slang_t, disney_frag_slang_t> rasterizer;
    rasterizer.setRasterState({.topology = RasterTopology::TriangleStrip});
    rasterizer.uses({{radiance, ResourceAccess::Read}, {irradiance, ResourceAccess::Read}});

    HardwareExecutor render_executor;
    HardwareDisplayer display(native_window, PresentDevicePreference::RenderDevice);

    const glm::vec3 target(0.0f);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    constexpr float yaw = glm::pi<float>(), pitch = 0.42f * glm::pi<float>();
    const glm::vec3 direction(std::sin(pitch) * std::sin(yaw), std::cos(pitch), std::sin(pitch) * std::cos(yaw));
    const auto eye = target + direction * 13.5f;
    const auto view = glm::lookAtLH(eye, target, up);
    const glm::vec4 camera_position(eye, 1.0f);
    const glm::vec4 light_direction(glm::normalize(glm::vec3(-0.35f, 0.55f, -0.75f)), 0.0f);
    rasterizer.vsp.camPos = camera_position;
    rasterizer.vsp.lightDir = light_direction;
    rasterizer.vsp.lightCol = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    rasterizer.vsp.envMtx = glm::mat4(1.0f);
    rasterizer.vsp.exposurePad = glm::vec4(0.0f);

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
            final_output_image.setClearColor(0.5f, 0.5f, 0.5f, 1.0f);
            rasterizer.outColor = final_output_image;
            auto projection = glm::perspectiveLH(glm::radians(45.0f), static_cast<float>(width) / height, 0.1f, 100.0f);
            projection[1][1] *= -1.0f;
            rasterizer.vsp.proj_view = projection * view;
        }
        if (frame == 0) start = glfwGetTime();
        const double seconds = fixed_dt ? frame * double(fixed_dt) : glfwGetTime() - start;
        ++frame;
        const int phase = static_cast<int>(std::fmod(seconds, 6.0) / 1.5);
        const float direct = (phase & 1) == 0, ibl = (phase & 2) == 0;
        rasterizer.vsp.flags = glm::vec4(direct, direct, ibl, ibl);
        auto batch = rasterizer.beginBatch();

        for (const auto& draw : draws)
            batch.draw(sphere, {}, draw);

        render_executor << std::move(batch);
        if (offscreen) {
            render_executor.commit();
        } else if (!display.present(render_executor, final_output_image)) {
            continue;
        }
        if (remaining_frames > 0 && --remaining_frames == 0) break;
    }

    render_executor.waitForDeferredResources();
    if (const char* filename = std::getenv("HORIZON_CAPTURE_PATH"); filename && *filename && final_output_image) {
        const std::filesystem::path path(filename);
        const uint32_t byte_count = width * height * 8;
        HardwareBuffer readback(byte_count, BufferUsage::StorageBuffer);
        HardwareExecutor copy;
        copy.wait(render_executor);
        copy << final_output_image.copyTo(readback);
        copy.commit().waitForDeferredResources();
        std::vector<uint16_t> half(static_cast<size_t>(width) * height * 4);
        if (!readback.copyToData(half.data(), byte_count)) throw std::runtime_error("Capture readback failed");
        std::vector<unsigned char> rgba(half.size());
        for (size_t i = 0; i < half.size(); ++i)
            rgba[i] = static_cast<unsigned char>(std::clamp(glm::unpackHalf1x16(half[i]), 0.0f, 1.0f) * 255.0f + 0.5f);
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        if (!stbi_write_png(path.string().c_str(), width, height, 4, rgba.data(), width * 4))
            throw std::runtime_error("Capture PNG write failed");
    }
}
