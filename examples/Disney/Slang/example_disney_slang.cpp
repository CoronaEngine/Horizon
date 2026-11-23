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

// Disney Principled PBR chart: 10 parameter rows x 11 values from 0 to 1.
constexpr int chart_row_count = 10;
constexpr int chart_col_count = 11;

struct DisneySlangSphereParameters {
    glm::mat4 model;
    glm::vec4 mat0, mat1, mat2;
    float clearcoatGloss;
};
static_assert(offsetof(DisneySlangSphereParameters, clearcoatGloss) == 28 * sizeof(uint32_t));
static_assert(sizeof(DisneySlangSphereParameters) == 29 * sizeof(uint32_t));

static RasterGeometry make_sphere();
static DisneySlangSphereParameters sphere_parameters(int row, int col);
static HardwareImage load_cubemap(const char* filename);

static int env_int(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !*value) return 0;
    char* end{};
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end || parsed < 0 || parsed > 10000000)
        throw std::invalid_argument(std::string("Invalid ") + name);
    return static_cast<int>(parsed);
}

static auto open_window(int width, int height, const char* title) {
    const bool hidden = env_int("HORIZON_BENCH_HIDDEN") != 0;
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

void run_example_disney_slang()
{
    struct DrawParameters {
        DisneySlangSphereParameters sphere;
        uint32_t texCubeIndex, texCubeIrrIndex;
    };
    static_assert(offsetof(DrawParameters, texCubeIndex) == 29 * sizeof(uint32_t));
    static_assert(offsetof(DrawParameters, texCubeIrrIndex) == 30 * sizeof(uint32_t));

    constexpr auto mode = "disney_slang";
    auto window = open_window(1920, 1080, mode);
    int width = 0, height = 0;
    HardwareImage final_output_image;

    HardwareImage radiance = load_cubemap("bolonga_lod.dds");
    HardwareImage irradiance = load_cubemap("bolonga_irr.dds");
    auto sphere = make_sphere();

    RasterizerPipeline<disney_vert_slang_t, disney_frag_slang_t> rasterizer;
    rasterizer.setRasterState({.topology = RasterTopology::TriangleStrip});
    const uint32_t cube_index = radiance.storeDescriptor();
    const uint32_t irradiance_index = irradiance.storeDescriptor();
    rasterizer.uses({{radiance, ResourceAccess::Read}, {irradiance, ResourceAccess::Read}});

    HardwareExecutor render_executor;
    HardwareDisplayer display(native_handle(window.get()), PresentDevicePreference::RenderDevice);

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

    int remaining_frames = env_int("HORIZON_PRESENT_FRAMES");
    const bool offscreen = env_int("HORIZON_BENCH_OFFSCREEN") != 0;
    float fixed_dt = 0.0f;
    if (const char* value = std::getenv("HORIZON_FIXED_DT"); value && *value) {
        fixed_dt = std::stof(value);
        if (!std::isfinite(fixed_dt) || fixed_dt < 0) throw std::invalid_argument("Invalid HORIZON_FIXED_DT");
    }
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

        for (int row = 0; row < chart_row_count; ++row)
        {
            for (int col = 0; col < chart_col_count; ++col)
            {
                const DrawParameters draw{sphere_parameters(row, col), cube_index, irradiance_index};
                batch.draw(sphere, {}, draw);
            }
        }

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

static uint32_t read_u32(const std::vector<std::byte>& bytes, size_t offset)
{
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

static HardwareImage load_cubemap(const char* filename)
{
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "env" / filename;
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

    if (bytes.size() < dx10_payload_offset || read_u32(bytes, 0) != dds_magic)
        throw std::runtime_error("Not a DDS file: " + path.string());

    const uint32_t height = read_u32(bytes, 12);
    const uint32_t width = read_u32(bytes, 16);
    const uint32_t mip_count = std::max(1u, read_u32(bytes, 28));
    const uint32_t fourcc = read_u32(bytes, 84);
    const uint32_t caps2 = read_u32(bytes, 112);

    if (fourcc != fourcc_dx10 || read_u32(bytes, 128) != dxgi_format_rgba16_float)
        throw std::runtime_error("Expected DX10 RGBA16F DDS: " + path.string());
    if ((caps2 & caps2_cubemap) == 0 || width != height)
        throw std::runtime_error("Expected cubemap DDS: " + path.string());

    HardwareImageCreateInfo desc(width, height, ImageFormat::RGBA16_FLOAT);
    desc.arrayLayers = 6;
    desc.cube = true;
    desc.linearFiltering = true;
    desc.mipLevels = static_cast<int>(mip_count);
    desc.usage = ImageUsage::SampledImage;
    HardwareImage image(desc);
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
            executor << staging.copyTo(image, offset, face, mip);
            offset += mip_bytes;
        }
    }
    executor.commit().waitForDeferredResources();
    return image;
}

static RasterGeometry make_sphere()
{
    constexpr int slices = 48, stacks = 24;
    // Each unit-sphere position also supplies its original object-space normal.
    std::vector<std::array<float, 3>> vertices;
    std::vector<uint16_t> indices;
    vertices.reserve(static_cast<size_t>((slices + 1) * (stacks + 1)));
    indices.reserve(static_cast<size_t>(stacks * (slices + 1) * 2 + (stacks - 1) * 2));

    for (int y = 0; y <= stacks; ++y)
    {
        const float v = static_cast<float>(y) / static_cast<float>(stacks);
        const float phi = v * glm::pi<float>();
        const float sin_phi = std::sin(phi);
        const float cos_phi = std::cos(phi);

        for (int x = 0; x <= slices; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(slices);
            const float theta = u * glm::two_pi<float>();
            const float sin_theta = std::sin(theta);
            const float cos_theta = std::cos(theta);

            vertices.push_back({ sin_phi * cos_theta, cos_phi, sin_phi * sin_theta });
        }
    }

    for (int y = 0; y < stacks; ++y)
    {
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

    return RasterGeometry::upload(vertices, indices);
}

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

static DisneySlangSphereParameters sphere_parameters(int row, int col)
{
    enum class ChartParam : int
    {
        Subsurface = 0,
        Metallic,
        Specular,
        SpecularTint,
        Roughness,
        Anisotropic,
        Sheen,
        SheenTint,
        Clearcoat,
        ClearcoatGloss,
    };

    constexpr float sphere_scale = 0.42f, spacing = 1.12f;
    const float x = (static_cast<float>(col) - (chart_col_count - 1) * 0.5f) * spacing;
    const float y = ((chart_row_count - 1) * 0.5f - static_cast<float>(row)) * spacing;
    DisneySlangSphereParameters draw{};
    draw.model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f))
               * glm::scale(glm::mat4(1.0f), glm::vec3(sphere_scale));
    draw.mat0 = glm::vec4(chart_row_colors[row], 0.0f);  // base color and metallic
    draw.mat1 = glm::vec4(0.5f, 0.5f, 0.0f, 0.0f);     // roughness, specular, specular tint, subsurface
    draw.mat2 = glm::vec4(0.0f, 0.0f, 0.5f, 0.0f);     // anisotropic, sheen, sheen tint, clearcoat
    draw.clearcoatGloss = 1.0f;

    const float t = static_cast<float>(col) / static_cast<float>(chart_col_count - 1);
    switch (static_cast<ChartParam>(row))
    {
    case ChartParam::Subsurface: draw.mat1.w = t; break;
    case ChartParam::Metallic: draw.mat0.w = t; break;
    case ChartParam::Specular: draw.mat1.y = t; break;
    case ChartParam::SpecularTint: draw.mat1.z = t; break;
    case ChartParam::Roughness: draw.mat1.x = t; break;
    case ChartParam::Anisotropic: draw.mat2.x = t; break;
    case ChartParam::Sheen: draw.mat2.y = t; break;
    case ChartParam::SheenTint: draw.mat2.z = t; break;
    case ChartParam::Clearcoat: draw.mat2.w = t; break;
    case ChartParam::ClearcoatGloss:
        draw.mat2.w = 1.0f;
        draw.clearcoatGloss = t;
        break;
    }

    return draw;
}
