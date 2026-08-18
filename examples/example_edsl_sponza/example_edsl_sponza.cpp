// example_sponza 的 EDSL 版:Disney BRDF + RSM 单次弹射 + SSR + SSAO + bloom,
// 白天斜射平行光,与 GLSL 版(example_sponza)同一资产、同一管线结构。
//
// EDSL 能力边界决定的结构性差异(其余逻辑逐段对齐 GLSL 版):
//   - MRT:与 GLSL 版相同,RSM 与 G-buffer 各为单 pass 三附件(片元里按
//     顺序多次 tex << value,location = 调用顺序)。历史上曾拆成单目标
//     pass 规避 device lost,根因是引擎的 renderTargetLocation 跨管线残留
//     (RasterizedPipelineObject::parse 已修复,解析前复位)。
//   - 逐 draw 材质贴图:bindless 下 record() 时 auto-bind 把当前绑定纹理的
//     描述符索引写进随 draw 深拷的 push constant,因此每次 record 前重新给
//     Texture2D 代理赋值即可(等价 GLSL 版的 push constant 索引)。
//   - 无材质 flags:缺贴图的槽位绑 1x1 白图 / 平法线图,统一走"贴图 × 因子";
//     Intel 资产无 MASK 材质,镂空路径整个省去。
//   - 所有循环在 codegen 期用 C++ 循环展开成算术(RSM 24 采样、SSAO 16、
//     SSR 24 步 + 4 次精修、PCF 9、blur 3x3、bloom 13 taps),continue/break
//     一律换成 step()/mix() 掩码。
//   - 只做太阳平行光(GLSL 版的点光源省略,白天场景默认也是 0 个);
//     不做 G-buffer debug 视图切换。
//   - 顶点输入用 Aggregate(多参数路径有 location 反转的历史问题)。

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <limits>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include "Codegen/BuiltinVariate.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"

#include <glm/gtx/euler_angles.hpp>

namespace
{
ktm::fmat4x4 to_edsl_matrix(const glm::mat4& matrix)
{
    static_assert(sizeof(ktm::fmat4x4) == sizeof(glm::mat4));
    ktm::fmat4x4 result;
    std::memcpy(&result, &matrix, sizeof(result));
    return result;
}

ktm::fvec4 to_edsl_vec4(const glm::vec4& v) { return ktm::fvec4(v.x, v.y, v.z, v.w); }

constexpr uint32_t spz_width = 1280;
constexpr uint32_t spz_height = 720;
constexpr uint32_t spz_shadow_map_size = 1024;  // 从2048降低，减少4倍shadow pass开销

constexpr uint32_t hzms_magic = 0x534D5A48; // 'HZMS'
constexpr uint32_t hzms_version_legacy = 2;
constexpr uint32_t hzms_version_pbr = 3;

const std::filesystem::path spz_assets_dir =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";
inline std::filesystem::path pick_asset_root()
{
    const std::filesystem::path pbr = spz_assets_dir / "sponza2";
    if (std::filesystem::exists(pbr / "sponza.bin"))
        return pbr;
    return spz_assets_dir / "sponza";
}
const std::filesystem::path spz_asset_root = pick_asset_root();

// ============================================================================
// HZMS 容器(与 example_sponza 相同的加载器)
// ============================================================================

struct SponzaVertex
{
    std::array<float, 3> position;
    std::array<float, 3> normal;
    std::array<float, 4> tangent;
    std::array<float, 2> uv;
};
static_assert(sizeof(SponzaVertex) == 48, "SponzaVertex must match the HZMS vertex stride");

struct SponzaSubmesh
{
    uint32_t first_index;
    uint32_t index_count;
    int32_t material;
    uint32_t name_offset;
    std::array<float, 3> min;
    std::array<float, 3> max;
};
static_assert(sizeof(SponzaSubmesh) == 40);

struct SponzaMaterial
{
    int32_t base_color_texture;
    int32_t normal_texture;
    int32_t metal_rough_texture;
    int32_t mask_texture;
    std::array<float, 4> base_color_factor;
    float metallic;
    float roughness;
    uint32_t name_offset;
    uint32_t flags;
};
static_assert(sizeof(SponzaMaterial) == 48);

struct SponzaScene
{
    std::array<float, 3> min;
    std::array<float, 3> max;
    std::array<float, 3> camera_position;
    std::array<float, 3> camera_target;
    std::array<float, 3> camera_up;
    float camera_yfov;
    float camera_znear;
    float camera_zfar;
    std::array<float, 3> sun_direction;
    std::array<float, 3> sun_color;
    float sun_intensity;
};
static_assert(sizeof(SponzaScene) == 25 * sizeof(float));

struct SponzaAsset
{
    uint32_t version = hzms_version_pbr;
    SponzaScene scene {};
    std::vector<SponzaVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SponzaSubmesh> submeshes;
    std::vector<SponzaMaterial> materials;
    std::vector<std::string> texture_names;
};

template <typename T>
T read_pod(const std::vector<std::byte>& bytes, size_t& cursor)
{
    if (cursor + sizeof(T) > bytes.size())
        throw std::runtime_error("HZMS: unexpected end of file");
    T value {};
    std::memcpy(&value, bytes.data() + cursor, sizeof(T));
    cursor += sizeof(T);
    return value;
}

template <typename T>
void read_array(const std::vector<std::byte>& bytes, size_t& cursor, std::vector<T>& out, size_t count)
{
    const size_t byte_count = count * sizeof(T);
    if (cursor + byte_count > bytes.size())
        throw std::runtime_error("HZMS: unexpected end of file");
    out.resize(count);
    if (byte_count != 0)
        std::memcpy(out.data(), bytes.data() + cursor, byte_count);
    cursor += byte_count;
}

// 16-bit 索引化：HZMS 的索引是全局的（sponza 共 190448 顶点，远超 65535），
// 但每个 submesh 实际触及的顶点区间很窄。按 submesh 把索引重基到区间起点，
// 用 draw 的 vertex_offset 补回，索引本身就能压进 16-bit。
// 万一某个 submesh 的顶点跨度仍 >= 65536，就按三角形粒度继续切成多次 draw。
struct SponzaDrawRange
{
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    int32_t vertex_offset = 0;
};

// 返回：每个 submesh 对应的 draw 区间列表（与 asset.submeshes 同序、一一对应）。
// 同时把 asset.indices 就地重基后写入 out_indices（长度不变，只是宽度变 16-bit）。
std::vector<std::vector<SponzaDrawRange>> rebase_indices_to_16bit(
    const std::vector<uint32_t>& indices,
    const std::vector<SponzaSubmesh>& submeshes,
    std::vector<uint16_t>& out_indices)
{
    constexpr uint32_t max_span = 0x10000u; // 16-bit 索引能覆盖的顶点跨度

    out_indices.assign(indices.size(), 0);
    std::vector<std::vector<SponzaDrawRange>> ranges;
    ranges.reserve(submeshes.size());

    for (const SponzaSubmesh& submesh : submeshes)
    {
        std::vector<SponzaDrawRange> submesh_ranges;

        const uint32_t begin = submesh.first_index;
        const uint32_t end = submesh.first_index + submesh.index_count;
        uint32_t chunk_begin = begin;
        uint32_t chunk_min = std::numeric_limits<uint32_t>::max();
        uint32_t chunk_max = 0;

        // chunk 收尾：把 [chunk_begin, chunk_end) 的索引减去 chunk_min 后落盘。
        const auto flush = [&](uint32_t chunk_end) {
            if (chunk_end == chunk_begin)
                return;
            for (uint32_t i = chunk_begin; i < chunk_end; ++i)
                out_indices[i] = static_cast<uint16_t>(indices[i] - chunk_min);
            submesh_ranges.push_back({ chunk_begin, chunk_end - chunk_begin,
                                       static_cast<int32_t>(chunk_min) });
        };

        // 按三角形推进：切点只落在三角形边界，否则会把三角形撕开。
        for (uint32_t t = begin; t + 2 < end; t += 3)
        {
            const uint32_t a = indices[t];
            const uint32_t b = indices[t + 1];
            const uint32_t c = indices[t + 2];
            const uint32_t tri_min = std::min({ a, b, c });
            const uint32_t tri_max = std::max({ a, b, c });

            if (tri_max - tri_min >= max_span)
                throw std::runtime_error("HZMS: a single triangle spans more than 65535 vertices; "
                                         "cannot express it with 16-bit indices");

            const uint32_t new_min = std::min(chunk_min, tri_min);
            const uint32_t new_max = std::max(chunk_max, tri_max);
            if (new_max - new_min >= max_span)
            {
                flush(t);
                chunk_begin = t;
                chunk_min = tri_min;
                chunk_max = tri_max;
            }
            else
            {
                chunk_min = new_min;
                chunk_max = new_max;
            }
        }
        flush(end);

        ranges.push_back(std::move(submesh_ranges));
    }

    return ranges;
}

SponzaAsset load_hzms(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Failed to open Sponza mesh: " + path.string());

    const std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<size_t>(file_size));
    file.read(reinterpret_cast<char*>(bytes.data()), file_size);

    size_t cursor = 0;
    if (read_pod<uint32_t>(bytes, cursor) != hzms_magic)
        throw std::runtime_error("Not an HZMS file: " + path.string());

    const uint32_t version = read_pod<uint32_t>(bytes, cursor);
    if (version != hzms_version_legacy && version != hzms_version_pbr)
        throw std::runtime_error("HZMS version " + std::to_string(version) + " is not supported");

    const uint32_t vertex_count = read_pod<uint32_t>(bytes, cursor);
    const uint32_t index_count = read_pod<uint32_t>(bytes, cursor);
    const uint32_t submesh_count = read_pod<uint32_t>(bytes, cursor);
    const uint32_t material_count = read_pod<uint32_t>(bytes, cursor);
    const uint32_t texture_count = read_pod<uint32_t>(bytes, cursor);
    const uint32_t string_bytes = read_pod<uint32_t>(bytes, cursor);

    SponzaAsset asset;
    asset.version = version;
    asset.scene = read_pod<SponzaScene>(bytes, cursor);

    read_array(bytes, cursor, asset.vertices, vertex_count);
    read_array(bytes, cursor, asset.indices, index_count);
    read_array(bytes, cursor, asset.submeshes, submesh_count);
    read_array(bytes, cursor, asset.materials, material_count);

    std::vector<uint32_t> texture_name_offsets;
    read_array(bytes, cursor, texture_name_offsets, texture_count);

    std::vector<char> strings;
    read_array(bytes, cursor, strings, string_bytes);

    asset.texture_names.reserve(texture_count);
    for (uint32_t offset : texture_name_offsets)
    {
        if (offset >= strings.size())
            throw std::runtime_error("HZMS: texture name offset out of range");
        asset.texture_names.emplace_back(strings.data() + offset);
    }

    if (version == hzms_version_legacy)
    {
        for (SponzaMaterial& material : asset.materials)
        {
            material.metal_rough_texture = -1;
            material.metallic = 0.0f;
            material.roughness = 0.72f;
        }
    }

    return asset;
}

// ============================================================================
// KTX1 / DDS 加载(与 example_sponza 相同)
// ============================================================================

constexpr std::array<uint8_t, 12> ktx1_identifier = {
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
};

horizon::Format ktx_format_from_gl(uint32_t gl_internal_format)
{
    switch (gl_internal_format)
    {
    case 0x8E8C: return horizon::Format::BC7_UNORM;
    case 0x8E8D: return horizon::Format::BC7_UNORM_SRGB;
    case 0x8058: return horizon::Format::RGBA8_UNORM;
    case 0x8C43: return horizon::Format::SRGBA8_UNORM;
    default:
        throw std::runtime_error("Unsupported KTX internal format: " + std::to_string(gl_internal_format));
    }
}

horizon::HardwareImage create_ktx_texture(const std::filesystem::path& path, const std::string& name)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Failed to open texture: " + path.string());

    const std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<size_t>(file_size));
    file.read(reinterpret_cast<char*>(bytes.data()), file_size);

    if (bytes.size() < 64 || std::memcmp(bytes.data(), ktx1_identifier.data(), 12) != 0)
        throw std::runtime_error("Not a KTX1 file: " + path.string());

    size_t cursor = 12;
    const uint32_t endianness = read_pod<uint32_t>(bytes, cursor);
    if (endianness != 0x04030201)
        throw std::runtime_error("Unsupported KTX endianness: " + path.string());

    read_pod<uint32_t>(bytes, cursor); // glType
    read_pod<uint32_t>(bytes, cursor); // glTypeSize
    read_pod<uint32_t>(bytes, cursor); // glFormat
    const uint32_t gl_internal_format = read_pod<uint32_t>(bytes, cursor);
    read_pod<uint32_t>(bytes, cursor); // glBaseInternalFormat
    const uint32_t width = read_pod<uint32_t>(bytes, cursor);
    const uint32_t height = read_pod<uint32_t>(bytes, cursor);
    read_pod<uint32_t>(bytes, cursor); // pixelDepth
    read_pod<uint32_t>(bytes, cursor); // numberOfArrayElements
    const uint32_t faces = read_pod<uint32_t>(bytes, cursor);
    const uint32_t mip_count = std::max(1u, read_pod<uint32_t>(bytes, cursor));
    const uint32_t key_value_bytes = read_pod<uint32_t>(bytes, cursor);
    cursor += key_value_bytes;

    if (faces != 1)
        throw std::runtime_error("Only 2D KTX textures are supported: " + path.string());

    std::vector<std::byte> payload;
    std::vector<uint64_t> level_offsets;
    payload.reserve(bytes.size());
    for (uint32_t mip = 0; mip < mip_count; ++mip)
    {
        const uint32_t image_size = read_pod<uint32_t>(bytes, cursor);
        if (cursor + image_size > bytes.size())
            throw std::runtime_error("KTX level truncated: " + path.string());
        level_offsets.push_back(payload.size());
        payload.insert(payload.end(), bytes.begin() + static_cast<ptrdiff_t>(cursor),
                       bytes.begin() + static_cast<ptrdiff_t>(cursor + image_size));
        cursor += image_size;
        cursor += (4 - (image_size % 4)) % 4;
    }

    horizon::HardwareImageDesc desc = horizon::HardwareImageDesc::texture_2d(
        width, height, ktx_format_from_gl(gl_internal_format),
        horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferDst, name);
    desc.mip_levels = mip_count;

    horizon::HardwareImage image(desc);

    horizon::HardwareBufferDesc staging_desc;
    staging_desc.element_count = payload.size();
    staging_desc.element_size = 1;
    staging_desc.usage = horizon::BufferUsage_TransferSrc;
    staging_desc.cpu_access = horizon::CpuAccessMode::Write;
    horizon::HardwareBuffer staging(staging_desc, std::span<const std::byte>(payload));

    horizon::HardwareExecutor executor;
    horizon::HardwareStream stream = executor.stream();
    for (uint32_t mip = 0; mip < mip_count; ++mip)
        stream << image.copy_from(staging, level_offsets[mip], 0, mip);
    (void)(stream << horizon::commit());

    return image;
}

horizon::HardwareImage create_solid_texture(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                                    const std::string& name)
{
    horizon::HardwareImageDesc desc = horizon::HardwareImageDesc::texture_2d(
        1, 1, horizon::Format::RGBA8_UNORM,
        horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferDst, name);
    horizon::HardwareImage image(desc);
    const std::array<uint8_t, 4> pixel = { r, g, b, a };
    horizon::HardwareBufferDesc staging_desc;
    staging_desc.element_count = pixel.size();
    staging_desc.element_size = 1;
    staging_desc.usage = horizon::BufferUsage_TransferSrc;
    staging_desc.cpu_access = horizon::CpuAccessMode::Write;
    horizon::HardwareBuffer staging(
        staging_desc, std::span<const std::byte>(reinterpret_cast<const std::byte*>(pixel.data()), pixel.size()));
    horizon::HardwareExecutor executor;
    (void)(executor.stream() << image.copy_from(staging, 0, 0, 0) << horizon::commit());
    return image;
}

struct CubeMapData
{
    uint32_t size = 0;
    uint32_t mip_count = 0;
    std::vector<std::byte> payload;
};

CubeMapData load_dds_cube_rgba16f(const std::filesystem::path& path)
{
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

    const auto read_u32 = [&bytes](size_t offset) {
        uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    };

    if (bytes.size() < dx10_payload_offset || read_u32(0) != dds_magic)
        throw std::runtime_error("Not a DDS file: " + path.string());

    const uint32_t height = read_u32(12);
    const uint32_t width = read_u32(16);
    const uint32_t mip_count = std::max(1u, read_u32(28));

    if (read_u32(84) != fourcc_dx10 || read_u32(128) != dxgi_format_rgba16_float)
        throw std::runtime_error("Expected DX10 RGBA16F DDS: " + path.string());
    if ((read_u32(112) & caps2_cubemap) == 0 || width != height)
        throw std::runtime_error("Expected cubemap DDS: " + path.string());

    CubeMapData data;
    data.size = width;
    data.mip_count = mip_count;
    data.payload.assign(bytes.begin() + dx10_payload_offset, bytes.end());
    return data;
}

horizon::HardwareImage create_cubemap_image(const CubeMapData& dds, const std::string& name)
{
    constexpr uint32_t bytes_per_pixel = 8;

    horizon::HardwareImageDesc desc = horizon::HardwareImageDesc::cube(
        dds.size, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferDst, name);
    desc.mip_levels = dds.mip_count;

    horizon::HardwareImage image(desc);

    horizon::HardwareBufferDesc staging_desc;
    staging_desc.element_count = dds.payload.size();
    staging_desc.element_size = 1;
    staging_desc.usage = horizon::BufferUsage_TransferSrc;
    staging_desc.cpu_access = horizon::CpuAccessMode::Write;
    horizon::HardwareBuffer staging(staging_desc, std::span<const std::byte>(dds.payload));

    horizon::HardwareExecutor executor;
    horizon::HardwareStream stream = executor.stream();
    uint64_t offset = 0;
    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t mip = 0; mip < dds.mip_count; ++mip)
        {
            const uint32_t dim = std::max(1u, dds.size >> mip);
            stream << image.copy_from(staging, offset, face, mip);
            offset += static_cast<uint64_t>(dim) * dim * bytes_per_pixel;
        }
    }
    (void)(stream << horizon::commit());

    return image;
}

// ============================================================================
// 全屏 quad 与相机(与 example_sponza 相同)
// ============================================================================

struct CornerVertex
{
    std::array<float, 3> corner;
};

const std::vector<CornerVertex> corner_vertices = {
    { { 0.0f, 0.0f, 0.0f } },
    { { 1.0f, 0.0f, 0.0f } },
    { { 1.0f, 1.0f, 0.0f } },
    { { 0.0f, 1.0f, 0.0f } },
};
const std::vector<uint16_t> corner_indices = { 0, 2, 1, 0, 3, 2 };

glm::vec3 to_vec3(const std::array<float, 3>& v) { return glm::vec3(v[0], v[1], v[2]); }

constexpr float pi_half = 1.5707963f;

struct FlyCamera
{
    glm::vec3 position { 0.0f };
    float yaw = 0.0f;
    float pitch = 0.0f;
    float speed = 600.0f;

    glm::vec3 forward() const
    {
        return glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
    }

    glm::vec3 right() const
    {
        return glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward()));
    }

    glm::mat4 view() const
    {
        return glm::lookAtLH(position, position + forward(), glm::vec3(0.0f, 1.0f, 0.0f));
    }
};

struct InputState
{
    bool looking = false;
    double last_x = 0.0;
    double last_y = 0.0;
    float yaw_delta = 0.0f;
    float pitch_delta = 0.0f;
};

InputState g_input;

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int /*mods*/)
{
    if (button != GLFW_MOUSE_BUTTON_RIGHT)
        return;
    if (action == GLFW_PRESS)
    {
        g_input.looking = true;
        glfwGetCursorPos(window, &g_input.last_x, &g_input.last_y);
    }
    else if (action == GLFW_RELEASE)
    {
        g_input.looking = false;
    }
}

void cursor_callback(GLFWwindow* /*window*/, double x, double y)
{
    if (!g_input.looking)
        return;
    constexpr float sensitivity = 0.0025f;
    g_input.yaw_delta += static_cast<float>(x - g_input.last_x) * sensitivity;
    g_input.pitch_delta -= static_cast<float>(y - g_input.last_y) * sensitivity;
    g_input.last_x = x;
    g_input.last_y = y;
}

void update_camera(GLFWwindow* window, FlyCamera& camera, float dt)
{
    camera.yaw += g_input.yaw_delta;
    camera.pitch = std::clamp(camera.pitch + g_input.pitch_delta, -pi_half * 0.98f, pi_half * 0.98f);
    g_input.yaw_delta = 0.0f;
    g_input.pitch_delta = 0.0f;

    const float boost = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 4.0f : 1.0f;
    const float step = camera.speed * boost * dt;
    const glm::vec3 forward = camera.forward();
    const glm::vec3 right = camera.right();

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.position += forward * step;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.position -= forward * step;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.position += right * step;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.position -= right * step;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        camera.position.y += step;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        camera.position.y -= step;
}

// 与 GLSL 版相同的"定妆参数"(live ImGui session 里调出的白天氛围)。
struct Tuning
{
    float sun_elevation_deg = 79.044f;
    float sun_azimuth_deg = -0.333f;
    float sun_intensity = 7.83f;
    glm::vec3 sun_color { 0.85784316f, 0.724044f, 0.5634852f };
    bool sun_shadow = true;
    float shadow_bias = 0.0027f;

    bool rsm_enable = true;
    float rsm_intensity = 160.0f;
    float rsm_radius = 0.085f;

    // 场景网格是 diffuse-only(specular 置 0),这些参数留给金属镜面池。
    float disney_specular = 0.0f;
    float disney_specular_tint = 0.0f;
    float disney_sheen = 0.0f;
    float disney_sheen_tint = 0.5f;
    float disney_clearcoat = 0.0f;
    float disney_clearcoat_gloss = 0.8f;
    float disney_subsurface = 0.0f;
    float disney_anisotropic = 0.0f;

    bool mirror_enable = true;
    float mirror_roughness = 0.065f;
    bool ssr_enable = true;
    float ssr_intensity = 0.982f;
    float ssr_max_distance = 26.228758f;
    float ssr_thickness = 0.16691028f;
    bool ssr_jitter = false;
    float ssr_fresnel_power = 5.0f;

    float irradiance_scale = 0.142f;
    float radiance_scale = 0.573f;
    float env_rotation_deg = 89.825f;
    glm::vec3 ambient_floor { 0.010f, 0.014f, 0.022f };

    float ssao_radius = 0.875f;
    float ssao_bias = 0.025f;
    float ssao_intensity = 2.0f;
    float ssao_blur_depth_sigma = 0.2861319f;

    float bloom_threshold = 0.85f;
    float bloom_intensity = 0.108f;
    float bloom_radius = 3.0f;
    float output_contrast = 1.023f;
    float output_saturation = 1.08f;
    float output_temperature = 0.02f;
    float vignette = 0.10f;
    float exposure = 0.568f;
};

} // namespace

using namespace EmbeddedShader;

// 顶点输入(48B HZMS 布局;用 Aggregate 保证 location 与成员顺序一致)
struct EdslSponzaVertexIn
{
    Float3 pos;
    Float3 normal;
    Float4 tangent;
    Float2 uv;
};

// 全屏 quad 的顶点输入
struct EdslCornerIn
{
    Float3 corner;
};

// RSM pass 的 VS 输出
struct EdslRsmVertOut
{
    Float4 v_clip;
    Float3 v_normal;
    Float2 v_uv;
};

// G-buffer pass 的 VS 输出
struct EdslGeomVertOut
{
    Float4 v_clip;
    Float3 v_normal;
    Float3 v_tangent;
    Float3 v_bitangent;
    Float2 v_uv;
};

// 全屏 pass 的 VS 输出
struct EdslQuadVertOut
{
    Float2 v_uv;
};

void run_example_edsl_sponza()
{
    const SponzaAsset asset = load_hzms(spz_asset_root / "sponza.bin");
    std::printf("[edsl_sponza] %s (HZMS v%u): %zu vertices, %zu indices, %zu submeshes, %zu materials\n",
                spz_asset_root.filename().string().c_str(), asset.version,
                asset.vertices.size(), asset.indices.size(), asset.submeshes.size(),
                asset.materials.size());

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(spz_width, spz_height, "Horizon Sponza [EDSL]", nullptr, nullptr);
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_callback);

    horizon::HardwareBuffer scene_vb =
        horizon::HardwareBuffer::vertex(asset.vertices, "edsl_sponza.scene.vb");
    // 16-bit 重基：索引写成 submesh 区间相对，vertex_offset 在 draw 时补回。
    std::vector<uint16_t> scene_indices_16;
    const std::vector<std::vector<SponzaDrawRange>> submesh_ranges =
        rebase_indices_to_16bit(asset.indices, asset.submeshes, scene_indices_16);
    horizon::HardwareBuffer scene_ib =
        horizon::HardwareBuffer::index(scene_indices_16, "edsl_sponza.scene.ib");
    horizon::HardwareBuffer quad_vb =
        horizon::HardwareBuffer::vertex(corner_vertices, "edsl_sponza.quad.vb");
    horizon::HardwareBuffer quad_ib =
        horizon::HardwareBuffer::index(corner_indices, "edsl_sponza.quad.ib");

    // 材质贴图 + 缺省贴图(白 / 平法线)
    std::vector<horizon::HardwareImage> textures;
    textures.reserve(asset.texture_names.size());
    for (const std::string& name : asset.texture_names)
        textures.push_back(create_ktx_texture(spz_asset_root / "textures" / name, "edsl_sponza." + name));

    horizon::HardwareImage white_texture = create_solid_texture(255, 255, 255, 255, "edsl_sponza.white");
    horizon::HardwareImage flat_normal_texture = create_solid_texture(128, 128, 255, 255, "edsl_sponza.flatn");

    const auto texture_or = [&](int32_t index, horizon::HardwareImage& fallback) -> horizon::HardwareImage& {
        if (index < 0 || static_cast<size_t>(index) >= textures.size())
            return fallback;
        return textures[static_cast<size_t>(index)];
    };

    // ---- 渲染目标 ----
    const auto rt_usage =
        horizon::ImageUsage_ColorAttachment | horizon::ImageUsage_Sampled;

    // RSM(单目标 ×3 共享深度)
    horizon::HardwareImage rsm_depth_map(horizon::HardwareImageDesc::texture_2d(
        spz_shadow_map_size, spz_shadow_map_size, horizon::Format::R32_FLOAT, rt_usage, "edsl_sponza.rsm.depth"));
    rsm_depth_map.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f);
    horizon::HardwareImage rsm_normal_map(horizon::HardwareImageDesc::texture_2d(
        spz_shadow_map_size, spz_shadow_map_size, horizon::Format::RGBA8_UNORM, rt_usage, "edsl_sponza.rsm.normal"));
    rsm_normal_map.set_clear_color(0.5f, 0.5f, 0.5f, 1.0f);
    horizon::HardwareImage rsm_flux_map(horizon::HardwareImageDesc::texture_2d(
        spz_shadow_map_size, spz_shadow_map_size, horizon::Format::RGBA8_UNORM, rt_usage, "edsl_sponza.rsm.flux"));
    rsm_flux_map.set_clear_color(0.0f, 0.0f, 0.0f, 0.0f);
    horizon::HardwareImage rsm_zbuffer(horizon::HardwareImageDesc::depth_attachment(
        spz_shadow_map_size, spz_shadow_map_size, horizon::Format::D32, "edsl_sponza.rsm.z"));
    rsm_zbuffer.set_clear_depth(1.0f, 0);

    // G-buffer(单目标 ×3 共享深度)
    horizon::HardwareImage gbuffer_albedo(horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, horizon::Format::RGBA8_UNORM, rt_usage, "edsl_sponza.gb.albedo"));
    gbuffer_albedo.set_clear_color(0.0f, 0.0f, 0.0f, 0.0f);
    horizon::HardwareImage gbuffer_normal(horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, horizon::Format::RGBA8_UNORM, rt_usage, "edsl_sponza.gb.normal"));
    gbuffer_normal.set_clear_color(0.5f, 0.5f, 0.5f, 1.0f);
    horizon::HardwareImage gbuffer_depth_val(horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, horizon::Format::R32_FLOAT, rt_usage, "edsl_sponza.gb.depthval"));
    gbuffer_depth_val.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f);
    horizon::HardwareImage gbuffer_zbuffer(horizon::HardwareImageDesc::depth_attachment(
        spz_width, spz_height, horizon::Format::D32, "edsl_sponza.gb.z"));
    gbuffer_zbuffer.set_clear_depth(1.0f, 0);

    // SSAO / 光照 / HDR / SSR 中间图
    horizon::HardwareImage ssao_raw(horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, horizon::Format::R32_FLOAT, rt_usage, "edsl_sponza.ssao.raw"));
    ssao_raw.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f);
    horizon::HardwareImage ssao_blurred(horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, horizon::Format::R32_FLOAT, rt_usage, "edsl_sponza.ssao.blur"));
    ssao_blurred.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f);

    horizon::HardwareImage light_buffer(horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, horizon::Format::RGBA16_FLOAT, rt_usage, "edsl_sponza.light"));
    light_buffer.set_clear_color(0.0f, 0.0f, 0.0f, 0.0f);

    horizon::HardwareImage hdr_buffer(horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, horizon::Format::RGBA16_FLOAT, rt_usage, "edsl_sponza.hdr"));
    hdr_buffer.set_clear_color(0.0f, 0.0f, 0.0f, 0.0f);

    horizon::HardwareImage ssr_linear_depth(horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, horizon::Format::R32_FLOAT, rt_usage, "edsl_sponza.ssr.lin"));
    horizon::HardwareImage ssr_buffer(horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, horizon::Format::RGBA16_FLOAT, rt_usage, "edsl_sponza.ssr.buf"));

    horizon::HardwareImage final_output_image(horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_ColorAttachment |
            horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferSrc |
            horizon::ImageUsage_TransferDst,
        "edsl_sponza.output"));
    final_output_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    // 环境 probe
    const std::filesystem::path env_root = spz_assets_dir / "env";
    const CubeMapData irradiance_dds = load_dds_cube_rgba16f(env_root / "day_clouds_irr.dds");
    const CubeMapData radiance_dds = load_dds_cube_rgba16f(env_root / "day_clouds_lod.dds");
    horizon::HardwareImage env_irradiance = create_cubemap_image(irradiance_dds, "edsl_sponza.env.irr");
    horizon::HardwareImage env_radiance = create_cubemap_image(radiance_dds, "edsl_sponza.env.lod");

    // ========================================================================
    // EDSL:公共辅助
    // ========================================================================

    // 说明:EDSL 的 auto 中间量是表达式树节点,每次使用都会在生成端整棵重复
    // 展开(生成器无子表达式缓存);多次使用的量必须"先落变量"(声明后赋值,
    // 生成真正的局部变量),否则生成源码乘法级膨胀、启动编译极慢。
    // 下面所有 lambda 均按此规则书写:复用 ≥2 次的量一律落变量。
    auto smoothstep_f = [&](Float edge0, Float edge1, Float x) {
        Float t;
        t = clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
        return t * t * (3.0f - 2.0f * t);
    };

    auto exp_f = [&](Float x) { return exp2(x * Float(1.4426950408889634f)); };

    // EDSL 库没有 cross,手写(参数先落变量:实参可能是复杂表达式,
    // 分量访问会把它重复展开 3 次)
    auto cross_f = [&](Float3 a, Float3 b) {
        Float3 av;
        av = a;
        Float3 bv;
        bv = b;
        return Float3(av->y * bv->z - av->z * bv->y,
                      av->z * bv->x - av->x * bv->z,
                      av->x * bv->y - av->y * bv->x);
    };

    // 定妆值按 Intel PBR 场景(米制);旧 cm 级资产在下面按场景尺度重标定。
    // 必须声明在所有 shader lambda 之前:$IF 的编译期剪枝 detector 是 [&] 捕获、
    // 与管线同寿命、每帧求值,所以条件变量要看得见且活得比管线长。
    Tuning tuning;

    // 编译期剪枝用的 CPU 侧开关。每帧从 tuning 同步一次(在 record 之前),
    // 供 $IF(cpu_xxx) 走剪枝重载 —— 注意 $IF 的重载决议:裸 bool 走剪枝,
    // VariateProxy<bool> 走运行时分支,两者外观相同但语义完全不同。
    struct PruneFlags
    {
        bool sun_shadow = true;   // 关闭时整段 PCF 采样被编译掉
        bool rsm = true;          // 关闭时整段 RSM gather(32 次采样)被编译掉
        bool ssr = true;          // 关闭时整段 SSR march 被编译掉
        bool ssr_jitter = false;  // 默认 false → 默认就能剪掉抖动计算
        bool disney_extra = false; // sheen/clearcoat/subsurface/aniso 任一非零时才需要
    } prune;

    // ========================================================================
    // Pass 0:RSM(单 pass MRT:depth / normal / flux)
    // ========================================================================

    Float4x4 rsm_light_view_proj;

    Float4 rsm_albedo_factor;
    rsm_albedo_factor.as_push_constant();

    Texture2D<ktm::fvec4> rsm_base_color_tex; // 逐 draw 重新绑定

    Texture2D<ktm::fvec4> rsm_depth_out = rsm_depth_map;
    Texture2D<ktm::fvec4> rsm_normal_out = rsm_normal_map;
    Texture2D<ktm::fvec4> rsm_flux_out = rsm_flux_map;

    auto rsm_vert = [&](Aggregate<EdslSponzaVertexIn> vin) {
        Aggregate<EdslRsmVertOut> out;
        auto clip = mul(rsm_light_view_proj, Float4(vin->pos, 1.f));
        position() = clip;
        out->v_clip = clip;
        out->v_normal = vin->normal;
        out->v_uv = vin->uv;
        return out;
    };

    // MRT:三次 << 依调用顺序落在 location 0/1/2(depth / normal / flux)
    auto rsm_frag = [&](Aggregate<EdslRsmVertOut> in) {
        auto depth = in->v_clip->z / in->v_clip->w;
        rsm_depth_out << Float4(depth, 0.f, 0.f, 1.f);
        auto n = normalize(in->v_normal);
        rsm_normal_out << Float4(n * Float(0.5f) + Float3(0.5f, 0.5f, 0.5f), 1.f);
        auto albedo = texture(rsm_base_color_tex, in->v_uv)->xyz() * rsm_albedo_factor->xyz();
        rsm_flux_out << Float4(albedo, 1.f);
    };

    // ========================================================================
    // Pass 1:G-buffer(单 pass MRT:albedo+metallic / normal+roughness / depth)
    // ========================================================================

    Float4x4 gb_view_proj;

    Float4 gb_base_factor;   // rgb: baseColorFactor
    Float4 gb_mr_factor;     // x: metallicFactor, y: roughnessFactor
    gb_base_factor.as_push_constant();
    gb_mr_factor.as_push_constant();

    Texture2D<ktm::fvec4> gb_base_color_tex;
    Texture2D<ktm::fvec4> gb_normal_tex;
    Texture2D<ktm::fvec4> gb_mr_tex;

    Texture2D<ktm::fvec4> gb_albedo_out = gbuffer_albedo;
    Texture2D<ktm::fvec4> gb_normal_out = gbuffer_normal;
    Texture2D<ktm::fvec4> gb_depth_out = gbuffer_depth_val;

    auto gb_vert = [&](Aggregate<EdslSponzaVertexIn> vin) {
        Aggregate<EdslGeomVertOut> out;
        auto clip = mul(gb_view_proj, Float4(vin->pos, 1.f));
        position() = clip;
        out->v_clip = clip;
        auto n = normalize(vin->normal);
        auto t = normalize(vin->tangent->xyz() - n * dot(n, vin->tangent->xyz()));
        out->v_normal = n;
        out->v_tangent = t;
        out->v_bitangent = cross_f(n, t) * vin->tangent->w;
        out->v_uv = vin->uv;
        return out;
    };

    // MRT:albedo+metallic(0) / normal+roughness(1) / device depth(2)
    auto gb_frag = [&](Aggregate<EdslGeomVertOut> in) {
        Float4 mr;
        mr = texture(gb_mr_tex, in->v_uv);
        auto base = texture(gb_base_color_tex, in->v_uv)->xyz() * gb_base_factor->xyz();
        gb_albedo_out << Float4(base, gb_mr_factor->x * mr->z); // glTF: B=metallic

        Float3 tn;
        tn = texture(gb_normal_tex, in->v_uv)->xyz() * Float(2.0f) - Float3(1.0f, 1.0f, 1.0f);
        auto n = normalize(normalize(in->v_tangent) * tn->x +
                           normalize(in->v_bitangent) * tn->y +
                           normalize(in->v_normal) * tn->z);
        gb_normal_out << Float4(n * Float(0.5f) + Float3(0.5f, 0.5f, 0.5f),
                                gb_mr_factor->y * mr->y); // glTF: G=roughness

        auto depth = in->v_clip->z / in->v_clip->w;
        gb_depth_out << Float4(depth, 0.f, 0.f, 1.f);
    };

    // ========================================================================
    // 全屏 quad VS(SSAO / blur / light / combine / SSR 共用)
    // ========================================================================

    auto quad_vert = [&](Aggregate<EdslCornerIn> vin) {
        Aggregate<EdslQuadVertOut> out;
        auto xy = Float2(vin->corner->xy());
        auto ndc = xy * Float(2.0f) - Float2(1.0f, 1.0f);
        position() = Float4(ndc->x, ndc->y, 0.f, 1.f);
        out->v_uv = xy;
        return out;
    };

    // ---- 共享 uniform(全屏 pass 用)----
    Float4x4 u_inv_view_proj;
    Float4x4 u_view;
    Float4x4 u_proj_view; // proj * view(SSAO 采样重投影)
    Float4x4 u_sun_view_proj;
    Float4x4 u_inv_sun_view_proj;
    Float4 u_camera_pos;
    Float4 u_sun_dir_ws;    // xyz: 光传播方向
    Float4 u_sun_radiance;  // rgb: color * intensity
    Float4 u_shadow_params; // x: texel world size, y: bias, z: 1/resolution, w: enable
    Float4 u_rsm_params;    // x: 半径(uv), y: 强度, z: enable, w: d² 钳制
    Float4 u_disney0;
    Float4 u_disney1;
    Float4 u_cc; // x: clearcoat a², y: ln(a²)(CPU 预计算,EDSL 无 log)
    Float4 u_ssao_params;   // x: radius, y: bias, z: intensity, w: unused
    Float4 u_ssao_blur;     // xy: texel, z: depth sigma, w: normal exponent
    Float4 u_depth_unpack;  // x: p32, y: -p22(viewZ = x/(d+y))
    Float4 u_env_params;    // x: irr scale, y: rad scale, z: env mips
    Float4 u_ambient;       // rgb: ambient floor
    Float4 u_ssr0;          // x: p00, y: p11, z: maxDistance, w: thickness
    Float4 u_ssr1;          // x: intensity(0=off), y: fresnelPower, z: jitter, w: frame
    Float4 u_ndc_to_view;
    Float4 u_view_r0;       // view 旋转三行
    Float4 u_view_r1;
    Float4 u_view_r2;
    Float4 u_post0;         // x: bloom threshold, y: bloom intensity, z: bloom radius(px), w: vignette
    Float4 u_post1;         // x: exposure, y: contrast, z: saturation, w: temperature
    Float4 u_texel;         // xy: 1/resolution

    Texture2D<ktm::fvec4> s_gb_albedo = gbuffer_albedo;
    Texture2D<ktm::fvec4> s_gb_normal = gbuffer_normal;
    Texture2D<ktm::fvec4> s_gb_depth = gbuffer_depth_val;
    Texture2D<ktm::fvec4> s_rsm_depth = rsm_depth_map;
    Texture2D<ktm::fvec4> s_rsm_normal = rsm_normal_map;
    Texture2D<ktm::fvec4> s_rsm_flux = rsm_flux_map;
    Texture2D<ktm::fvec4> s_ssao_raw = ssao_raw;
    Texture2D<ktm::fvec4> s_ssao_blurred = ssao_blurred;
    Texture2D<ktm::fvec4> s_light = light_buffer;
    Texture2D<ktm::fvec4> s_hdr = hdr_buffer;
    Texture2D<ktm::fvec4> s_ssr_lin = ssr_linear_depth;
    Texture2D<ktm::fvec4> s_ssr_buf = ssr_buffer;
    TextureCube<ktm::fvec4> s_env_irr = env_irradiance;
    TextureCube<ktm::fvec4> s_env_lod = env_radiance;

    Texture2D<ktm::fvec4> ssao_out = ssao_raw;
    Texture2D<ktm::fvec4> ssao_blur_out = ssao_blurred;
    Texture2D<ktm::fvec4> light_out = light_buffer;
    Texture2D<ktm::fvec4> combine_out = hdr_buffer;
    Texture2D<ktm::fvec4> ssr_lin_out = ssr_linear_depth;
    Texture2D<ktm::fvec4> ssr_trace_out = ssr_buffer;
    Texture2D<ktm::fvec4> final_out = final_output_image;

    auto world_from_depth = [&](Float2 uv, Float depth) {
        Float4 wpos4;
        wpos4 = mul(u_inv_view_proj, Float4(uv * Float(2.0f) - Float2(1.0f, 1.0f), depth, 1.f));
        return wpos4->xyz() / wpos4->w;
    };

    // ========================================================================
    // SSAO(16 采样半球核,黄金螺旋;GLSL 版为 24)
    // ========================================================================

    auto hash12 = [&](Float2 p) {
        Float2 pv;
        pv = p;
        Float3 p3;
        p3 = fract(Float3(pv->x, pv->y, pv->x) * Float(0.1031f));
        Float3 q;
        q = p3 + dot(p3, Float3(p3->y, p3->z, p3->x) + Float3(33.33f, 33.33f, 33.33f));
        return fract((q->x + q->y) * q->z);
    };

    auto ssao_frag = [&](Aggregate<EdslQuadVertOut> in) {
        Float2 uv;
        uv = in->v_uv;
        Float device_depth;
        device_depth = texture(s_gb_depth, uv)->x;
        auto is_sky = step(1.0f, device_depth);

        Float3 wpos;
        wpos = world_from_depth(uv, device_depth);
        Float3 normal;
        normal = normalize(texture(s_gb_normal, uv)->xyz() * Float(2.0f) - Float3(1.0f, 1.0f, 1.0f));

        Float angle;
        angle = hash12(uv * Float2(float(spz_width), float(spz_height))) * Float(6.2831853f);
        // helper 选择:法线接近 ±Y 时换 X 轴
        auto near_y = step(0.999f, abs(normal->y));
        Float3 base_tangent;
        base_tangent = normalize(cross_f(mix(Float3(0.f, 1.f, 0.f), Float3(1.f, 0.f, 0.f), near_y), normal));
        Float3 base_bitangent;
        base_bitangent = cross_f(normal, base_tangent);
        Float ca;
        ca = cos(angle);
        Float sa;
        sa = sin(angle);
        Float3 tangent;
        tangent = base_tangent * ca + base_bitangent * sa;
        Float3 bitangent;
        bitangent = base_bitangent * ca - base_tangent * sa;

        Float radius;
        radius = u_ssao_params->x;
        Float bias;
        bias = u_ssao_params->y;

        Float occlusion;
        occlusion = Float(0.0f);
        constexpr int kSsaoSamples = 24; // 对齐 GLSL sponza_ssao_frag.glsl:37
        for (int i = 0; i < kSsaoSamples; ++i)
        {
            const float fi = static_cast<float>(i) + 0.5f;
            const float cos_theta = 1.0f - fi / kSsaoSamples;
            const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
            const float phi = fi * 2.39996323f;
            const float kx = std::cos(phi) * sin_theta;
            const float ky = std::sin(phi) * sin_theta;
            const float kz = cos_theta;
            const float scale = 0.3f + 0.7f * (fi / kSsaoSamples) * (fi / kSsaoSamples);

            Float3 sample_pos;
            sample_pos = wpos + (tangent * Float(kx * scale) + bitangent * Float(ky * scale) +
                                 normal * Float(kz * scale)) * radius;

            Float4 clip;
            clip = mul(u_proj_view, Float4(sample_pos, 1.f));
            auto w_ok = step(1e-4f, clip->w);
            Float2 suv;
            suv = clip->xy() / max(clip->w, Float(1e-4f)) * Float(0.5f) + Float2(0.5f, 0.5f);
            auto in_x = step(0.0f, suv->x) * step(suv->x, Float(1.0f));
            auto in_y = step(0.0f, suv->y) * step(suv->y, Float(1.0f));
            Float2 suv_c;
            suv_c = min(max(suv, Float2(0.f, 0.f)), Float2(1.f, 1.f));

            Float scene_depth;
            scene_depth = texture(s_gb_depth, suv_c)->x;
            auto has_geo = Float(1.0f) - step(1.0f, scene_depth);
            Float3 scene_world;
            scene_world = world_from_depth(suv_c, scene_depth);

            auto sample_view_z = mul(u_view, Float4(sample_pos, 1.f))->z;
            auto scene_view_z = mul(u_view, Float4(scene_world, 1.f))->z;

            auto occluded = step(scene_view_z, sample_view_z - bias);
            auto separation = length(scene_world - sample_pos);
            auto range_check = Float(1.0f) - smoothstep_f(radius * Float(0.25f), radius, separation);

            occlusion = occlusion + occluded * range_check * w_ok * in_x * in_y * has_geo;
        }

        auto ao = Float(1.0f) - occlusion / Float(float(kSsaoSamples)) * u_ssao_params->z;
        ssao_out << Float4(mix(clamp(ao, 0.f, 1.f), Float(1.0f), is_sky), 0.f, 0.f, 1.f);
    };

    // SSAO blur:3x3 双边(GLSL 版为 5x5)
    auto view_depth_of = [&](Float device_depth) {
        return u_depth_unpack->x / (device_depth + u_depth_unpack->y);
    };

    auto ssao_blur_frag = [&](Aggregate<EdslQuadVertOut> in) {
        Float2 uv;
        uv = in->v_uv;
        Float center_depth;
        center_depth = texture(s_gb_depth, uv)->x;
        auto is_sky = step(1.0f, center_depth);
        Float center_view;
        center_view = view_depth_of(center_depth);
        Float3 center_normal;
        center_normal = normalize(texture(s_gb_normal, uv)->xyz() * Float(2.0f) - Float3(1.0f, 1.0f, 1.0f));
        Float sigma;
        sigma = max(u_ssao_blur->z, Float(1e-4f));
        Float2 blur_texel;
        blur_texel = u_ssao_blur->xy();

        Float total;
        total = Float(0.0f);
        Float weight_sum;
        weight_sum = Float(1e-4f);
        for (int y = -2; y <= 2; ++y) // 5x5,对齐 GLSL sponza_ssao_blur_frag.glsl:53
        {
            for (int x = -2; x <= 2; ++x)
            {
                const float spatial = std::exp(-0.5f * float(x * x + y * y) / 4.0f);
                Float2 suv;
                suv = min(max(uv + Float2(float(x), float(y)) * blur_texel,
                              Float2(0.f, 0.f)), Float2(1.f, 1.f));
                Float d;
                d = texture(s_gb_depth, suv)->x;
                auto has_geo = Float(1.0f) - step(1.0f, d);
                Float dd;
                dd = (view_depth_of(d) - center_view) / sigma;
                auto depth_w = exp_f(Float(-0.5f) * dd * dd);
                Float3 n;
                n = normalize(texture(s_gb_normal, suv)->xyz() * Float(2.0f) - Float3(1.0f, 1.0f, 1.0f));
                auto normal_w = pow(max(0.f, dot(center_normal, n)), u_ssao_blur->w);
                Float w;
                w = depth_w * normal_w * Float(spatial) * has_geo;
                total = total + texture(s_ssao_raw, suv)->x * w;
                weight_sum = weight_sum + w;
            }
        }
        ssao_blur_out << Float4(mix(total / weight_sum, Float(1.0f), is_sky), 0.f, 0.f, 1.f);
    };

    // ========================================================================
    // 光照 pass:太阳 Disney BRDF + PCF 阴影 + RSM 单次弹射(24 采样)
    // ========================================================================

    auto schlick_fresnel = [&](Float u) {
        Float m;
        m = clamp(Float(1.0f) - u, 0.f, 1.f);
        return m * m * m * m * m;
    };

    auto smith_ggx = [&](Float ndotv, float alpha_g) {
        const float a = alpha_g * alpha_g;
        Float nvv;
        nvv = ndotv;
        auto b = nvv * nvv;
        return Float(1.0f) / (nvv + sqrt(Float(a) + b - Float(a) * b));
    };

    auto smith_ggx_aniso = [&](Float ndotv, Float vdotx, Float vdoty, Float ax, Float ay) {
        Float nvv;
        nvv = ndotv;
        Float tx;
        tx = vdotx * ax;
        Float ty;
        ty = vdoty * ay;
        return Float(1.0f) / (nvv + sqrt(tx * tx + ty * ty + nvv * nvv));
    };

    auto light_frag = [&](Aggregate<EdslQuadVertOut> in) {
        Float2 uv;
        uv = in->v_uv;
        Float4 albedo_sample;
        albedo_sample = texture(s_gb_albedo, uv);
        Float4 packed_normal;
        packed_normal = texture(s_gb_normal, uv);
        Float device_depth;
        device_depth = texture(s_gb_depth, uv)->x;
        auto has_geo = Float(1.0f) - step(1.0f, device_depth);

        // baseColor 贴图是 BC7_SRGB,采样已线性化并直接写进 G-buffer;
        // 这里再 pow(2.2) 会二次线性化压坏材质响应。
        Float3 Cdlin;
        Cdlin = albedo_sample->xyz();
        Float metallic;
        metallic = albedo_sample->w;
        Float3 N;
        N = normalize(packed_normal->xyz() * Float(2.0f) - Float3(1.0f, 1.0f, 1.0f));
        Float roughness;
        roughness = clamp(packed_normal->w, 0.03f, 1.f);

        Float3 wpos;
        wpos = world_from_depth(uv, device_depth);
        Float3 V;
        V = normalize(u_camera_pos->xyz() - wpos);
        Float3 L;
        L = -normalize(u_sun_dir_ws->xyz());

        Float NdotL;
        NdotL = dot(N, L);
        Float NdotV;
        NdotV = dot(N, V);
        auto lit_mask = step(0.0f, NdotL) * step(0.0f, NdotV);
        Float nl;
        nl = max(NdotL, Float(1e-4f));
        Float nv;
        nv = max(NdotV, Float(1e-4f));

        // ---- Disney BRDF(方向固定为太阳)----
        // L+V 反向时半向量未定义,用掩码归零(GLSL 版为 early return)
        Float3 half_vec;
        half_vec = L + V;
        Float h_len2;
        h_len2 = dot(half_vec, half_vec);
        auto h_ok = step(1e-8f, h_len2);
        Float3 H;
        H = half_vec / sqrt(max(h_len2, Float(1e-8f)));
        Float NdotH;
        NdotH = clamp(dot(N, H), 0.f, 1.f);
        Float LdotH;
        LdotH = clamp(dot(L, H), 0.f, 1.f);

        auto specular = u_disney0->x;
        auto specular_tint = u_disney0->y;
        auto sheen = u_disney0->z;
        auto sheen_tint = u_disney0->w;
        auto clearcoat = u_disney1->x;
        auto subsurface = u_disney1->z;
        auto anisotropic = u_disney1->w;

        Float Cdlum;
        Cdlum = max(dot(Cdlin, Float3(0.3f, 0.6f, 0.1f)), Float(1e-4f));
        Float3 Ctint;
        Ctint = Cdlin / Cdlum;
        Float3 Cspec0;
        Cspec0 = mix(mix(Float3(1.f, 1.f, 1.f), Ctint, specular_tint) * (specular * Float(0.08f)), Cdlin, metallic);
        Float3 Csheen;
        Csheen = mix(Float3(1.f, 1.f, 1.f), Ctint, sheen_tint);

        Float FL;
        FL = schlick_fresnel(nl);
        Float FV;
        FV = schlick_fresnel(nv);
        Float ldh2r;
        ldh2r = LdotH * LdotH * roughness;
        Float Fd90;
        Fd90 = Float(0.5f) + ldh2r * Float(2.0f);
        auto Fd = mix(Float(1.0f), Fd90, FL) * mix(Float(1.0f), Fd90, FV);

        auto Fss = mix(Float(1.0f), ldh2r, FL) * mix(Float(1.0f), ldh2r, FV);
        auto ss = (Fss * (Float(1.0f) / (nl + nv) - Float(0.5f)) + Float(0.5f)) * Float(1.25f);

        // 各向异性切线框架
        Float3 cand;
        cand = cross_f(N, Float3(0.f, 1.f, 0.f));
        auto pick = step(dot(cand, cand), Float(1e-6f));
        Float3 X;
        X = normalize(mix(cand, cross_f(N, Float3(1.f, 0.f, 0.f)), pick));
        Float3 Y;
        Y = normalize(cross_f(N, X));

        Float aspect;
        aspect = sqrt(Float(1.0f) - anisotropic * Float(0.9f));
        Float r2;
        r2 = roughness * roughness;
        Float ax;
        ax = max(Float(0.001f), r2 / aspect);
        Float ay;
        ay = max(Float(0.001f), r2 * aspect);
        Float hdx;
        hdx = dot(H, X) / ax;
        Float hdy;
        hdy = dot(H, Y) / ay;
        Float denom_d;
        denom_d = hdx * hdx + hdy * hdy + NdotH * NdotH;
        auto Ds = Float(1.0f) / (Float(3.14159265f) * ax * ay * denom_d * denom_d);
        Float FH;
        FH = schlick_fresnel(LdotH);
        auto Fs = mix(Cspec0, Float3(1.f, 1.f, 1.f), FH);
        auto Gs = smith_ggx_aniso(nl, dot(L, X), dot(L, Y), ax, ay) *
                  smith_ggx_aniso(nv, dot(V, X), dot(V, Y), ax, ay);

        auto Fsheen = Csheen * (FH * sheen);

        // clearcoat(GTR1;a2 与 ln(a2) 由 CPU 算好经 u_cc 传入 —— EDSL 库没有 log)
        Float cc_a2;
        cc_a2 = u_cc->x;
        auto cc_t = Float(1.0f) + (cc_a2 - Float(1.0f)) * NdotH * NdotH;
        auto Dr = (cc_a2 - Float(1.0f)) / (Float(3.14159265f) * u_cc->y * cc_t);
        auto Fr = mix(Float(0.04f), Float(1.0f), FH);
        auto Gr = smith_ggx(nl, 0.25f) * smith_ggx(nv, 0.25f);

        auto diffuse_part = Cdlin * (mix(Fd, ss, subsurface) * (Float(1.0f) / Float(3.14159265f))) *
                            (Float(1.0f) - metallic);
        auto spec_part = Fs * (Gs * Ds) + Float3(1.f, 1.f, 1.f) * (Gr * Fr * Dr * clearcoat * Float(0.25f));
        Float3 brdf;
        brdf = (diffuse_part + (spec_part + Fsheen) * metallic) * h_ok;

        // ---- PCF 3x3 太阳阴影 ----
        Float4 sclip;
        sclip = mul(u_sun_view_proj, Float4(wpos + N * u_shadow_params->x, 1.f));
        Float3 sndc;
        sndc = sclip->xyz() / sclip->w;
        Float2 suv;
        suv = sndc->xy() * Float(0.5f) + Float2(0.5f, 0.5f);
        auto s_in = step(0.0f, suv->x) * step(suv->x, Float(1.0f)) *
                    step(0.0f, suv->y) * step(suv->y, Float(1.0f)) *
                    step(0.0f, sndc->z) * step(sndc->z, Float(1.0f));
        Float receiver;
        receiver = sndc->z - u_shadow_params->y;
        Float pcf;
        pcf = Float(0.0f);
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                Float2 tuv;
                tuv = min(max(suv + Float2(float(x), float(y)) * u_shadow_params->z,
                              Float2(0.f, 0.f)), Float2(1.f, 1.f));
                pcf = pcf + step(receiver, texture(s_rsm_depth, tuv)->x);
            }
        }
        Float shadow;
        shadow = mix(Float(1.0f), pcf / Float(9.0f), s_in * u_shadow_params->w);

        // ---- RSM 单次弹射(24 采样,黄金螺旋,ξ² 权重)----
        Float4 rclip;
        rclip = mul(u_sun_view_proj, Float4(wpos, 1.f));
        Float3 rndc;
        rndc = rclip->xyz() / rclip->w;
        Float2 ruv_center;
        ruv_center = rndc->xy() * Float(0.5f) + Float2(0.5f, 0.5f);
        auto r_in = step(0.0f, ruv_center->x) * step(ruv_center->x, Float(1.0f)) *
                    step(0.0f, ruv_center->y) * step(ruv_center->y, Float(1.0f));

        Float3 rsm_sum;
        rsm_sum = Float3(0.f, 0.f, 0.f);

        // GLSL 版这里是 `if (fsp.rsm_params.z < 0.5) return vec3(0);`
        // (sponza_light_frag.glsl:222) —— 关闭 RSM 时整段 32x3 次采样根本不执行。
        // 这里用**编译期剪枝**做得更彻底:条件是裸 C++ bool,关闭时这段代码不会出现在
        // 最终 shader 里(而不是运行时跳过)。$ELSE 必须写,否则分支体被静默丢弃。
        constexpr int kRsmSamples = 32; // 对齐 GLSL sponza_light_frag.glsl:63
        constexpr float kGolden = 0.61803398875f;
        constexpr float kTwoPi = 6.28318530718f;

        $IF(prune.rsm)
        {
            Float3 sun_travel;
            sun_travel = normalize(u_sun_dir_ws->xyz());
            Float rsm_radius_uv;
            rsm_radius_uv = u_rsm_params->x;
            Float rsm_clamp;
            rsm_clamp = u_rsm_params->w;
            for (int i = 0; i < kRsmSamples; ++i)
            {
                const float xi1 = (static_cast<float>(i) + 0.5f) / kRsmSamples;
                const float xi2 = std::fmod(static_cast<float>(i) * kGolden, 1.0f);
                const float ox = xi1 * std::cos(kTwoPi * xi2);
                const float oy = xi1 * std::sin(kTwoPi * xi2);
                const float weight = xi1 * xi1;

                Float2 ruv_raw;
                ruv_raw = ruv_center + Float2(ox, oy) * rsm_radius_uv;
                // 出界采样整个作废(GLSL 版为 continue),不 clamp 以免边缘重复计入
                auto ruv_in = step(0.0f, ruv_raw->x) * step(ruv_raw->x, Float(1.0f)) *
                              step(0.0f, ruv_raw->y) * step(ruv_raw->y, Float(1.0f));
                Float2 ruv;
                ruv = min(max(ruv_raw, Float2(0.f, 0.f)), Float2(1.f, 1.f));
                Float rdepth;
                rdepth = texture(s_rsm_depth, ruv)->x;
                auto valid = (Float(1.0f) - step(0.9999f, rdepth)) * ruv_in;

                Float4 xp4;
                xp4 = mul(u_inv_sun_view_proj, Float4(ruv * Float(2.0f) - Float2(1.0f, 1.0f), rdepth, 1.f));
                Float3 np;
                np = normalize(texture(s_rsm_normal, ruv)->xyz() * Float(2.0f) - Float3(1.0f, 1.0f, 1.0f));
                // flux 图经 sRGB 视图采样后已是线性 albedo,不再 pow;乘 VPL 的
                // 原始受光余弦,否则背光 texel 也当满能量像素光弹射。
                auto source_ndotl = max(0.f, dot(np, -sun_travel));
                auto flux = Float3(texture(s_rsm_flux, ruv)->xyz()) * source_ndotl;

                Float3 w;
                w = wpos - xp4->xyz() / xp4->w;
                Float d2;
                d2 = max(dot(w, w), rsm_clamp);
                auto e = max(0.f, dot(np, w)) * max(0.f, dot(N, -w)) / (d2 * d2);
                rsm_sum = rsm_sum + flux * (e * Float(weight)) * valid;
            }
        }
        $ELSE
        {
            rsm_sum = Float3(0.f, 0.f, 0.f);
        };

        // 增益与采样数解耦(× intensity / N),同 GLSL 版
        auto rsm_indirect = rsm_sum * u_sun_radiance->xyz() *
                            (u_rsm_params->y * Float(1.0f / float(kRsmSamples))) *
                            (Float(1.0f) / Float(3.14159265f)) * u_rsm_params->z * r_in;
        auto indirect = rsm_indirect * Cdlin * (Float(1.0f) - metallic);

        auto radiance = brdf * (max(NdotL, Float(0.0f)) * shadow * lit_mask) * u_sun_radiance->xyz() + indirect;
        light_out << Float4(radiance * has_geo, 1.f);
    };

    // ========================================================================
    // combine:直接光 + IBL × AO → 线性 HDR(a = SSR F0)
    // ========================================================================

    // env 绕 Y 旋转(UI 正向转环境 → 采样用逆旋转;u_env_params.w = 弧度)
    auto environment_direction = [&](Float3 world_dir) {
        Float3 dir;
        dir = world_dir;
        Float c;
        c = cos(u_env_params->w);
        Float s;
        s = sin(u_env_params->w);
        return Float3(c * dir->x - s * dir->z,
                      dir->y,
                      s * dir->x + c * dir->z);
    };

    auto combine_frag = [&](Aggregate<EdslQuadVertOut> in) {
        Float2 uv;
        uv = in->v_uv;
        Float4 albedo_sample;
        albedo_sample = texture(s_gb_albedo, uv);
        Float4 packed_normal;
        packed_normal = texture(s_gb_normal, uv);
        Float device_depth;
        device_depth = texture(s_gb_depth, uv)->x;
        Float ao;
        ao = texture(s_ssao_blurred, uv)->x;

        Float3 albedo; // already linear (BC7_SRGB sampling)
        albedo = albedo_sample->xyz();
        Float metallic;
        metallic = albedo_sample->w;
        Float roughness;
        roughness = packed_normal->w;
        Float is_sky;
        is_sky = step(1.0f, device_depth);

        Float3 wpos;
        wpos = world_from_depth(uv, device_depth);
        Float3 view_dir;
        view_dir = normalize(u_camera_pos->xyz() - wpos);
        Float3 normal;
        normal = normalize(packed_normal->xyz() * Float(2.0f) - Float3(1.0f, 1.0f, 1.0f));
        auto ndotv = clamp(dot(normal, view_dir), 0.f, 1.f);

        // env probe 是 RGBA16F 线性 HDR,直接用
        auto irradiance = Float3(texture(s_env_irr, environment_direction(normal))->xyz()) * u_env_params->x;

        auto reflected = normalize(view_dir * Float(-1.0f) + normal * (dot(normal, view_dir) * Float(2.0f)));
        auto mip = roughness * max(u_env_params->z - Float(1.0f), Float(0.0f));
        auto radiance_env = Float3(textureLod(s_env_lod, environment_direction(reflected), mip)->xyz()) * u_env_params->y;

        Float3 cspec;
        cspec = mix(Float3(0.04f, 0.04f, 0.04f), albedo, metallic);
        auto env_fresnel = cspec + (Float3(1.f, 1.f, 1.f) - cspec) *
                           (pow(Float(1.0f) - ndotv, Float(5.0f)) * (Float(1.0f) - roughness * Float(0.7f)));

        auto indirect_diffuse = albedo * (Float(1.0f) - metallic) * (irradiance + u_ambient->xyz()) * ao;
        // 光滑反射不该被缝隙 AO 压黑;lobe 越粗糙越贴近 AO。
        // 场景网格 diffuse-only,IBL 镜面只给显式金属镜面池(× metallic)。
        auto indirect_specular = radiance_env * env_fresnel * mix(Float(1.0f), ao, roughness) * metallic;

        auto shaded = Float3(texture(s_light, uv)->xyz()) + indirect_diffuse + indirect_specular;

        // 天空:通过屋顶开口看 env probe
        auto sky_dir = normalize(world_from_depth(uv, Float(1.0f)) - u_camera_pos->xyz());
        auto sky = Float3(texture(s_env_lod, environment_direction(sky_dir))->xyz()) * u_env_params->y;

        // alpha 是 SSR 的 F0/掩码:场景强制非金属,只有镜面池会步进
        auto reflectivity = metallic * dot(cspec, Float3(0.2126f, 0.7152f, 0.0722f)) * (Float(1.0f) - is_sky);
        combine_out << Float4(mix(shaded, sky, is_sky), reflectivity);
    };

    // ========================================================================
    // SSR:线性深度 → 步进(24 步 + 4 精修,全展开) → 合成 + bloom + ACES
    // ========================================================================

    auto ssr_lin_frag = [&](Aggregate<EdslQuadVertOut> in) {
        // viewZ = p32/(d - p22);Vulkan LH-ZO 下 d<1 时分子分母同负 → 正值,
        // d=1(清屏)时分母 →0⁻,商为大正数,clamp 到 far 即远平面填充值。
        auto d = texture(s_gb_depth, in->v_uv)->x;
        auto view_z = clamp(u_depth_unpack->x / (d + u_depth_unpack->y), 0.f, u_depth_unpack->z);
        ssr_lin_out << Float4(view_z, 0.f, 0.f, 1.f);
    };

    auto view_to_uv = [&](Float3 p) {
        Float3 pv;
        pv = p;
        auto ndc = Float2(u_ssr0->x * pv->x, u_ssr0->y * pv->y) / max(pv->z, Float(1e-4f));
        return ndc * Float(0.5f) + Float2(0.5f, 0.5f);
    };

    auto ssr_trace_frag = [&](Aggregate<EdslQuadVertOut> in) {
        Float2 uv;
        uv = in->v_uv;
        Float surface_f0;
        surface_f0 = texture(s_hdr, uv)->w;

        // GLSL 版在这里是 `if (surface_f0 <= 0.001) return;`(sponza_ssr_trace_compute.glsl:65),
        // 约 97% 的像素直接退出。EDSL 原先改写成 step() 掩码,导致 80 步 march 对每个像素
        // 全跑。这里用运行时 $IF 复原早退语义:条件是 VariateProxy<bool>,走运行时分支重载。
        // 结果量必须声明在分支外 —— 分支内只能给块外已声明的局部量赋值。
        Float3 out_color;
        out_color = Float3(0.f, 0.f, 0.f);
        Float out_weight;
        out_weight = Float(0.0f);

        $IF(surface_f0 > Float(0.001f))
        {

            Float view_z;
            view_z = texture(s_ssr_lin, uv)->x;
            Float3 view_pos;
            view_pos = Float3((uv->x * u_ndc_to_view->x + u_ndc_to_view->z) * view_z,
                              (uv->y * u_ndc_to_view->y + u_ndc_to_view->w) * view_z,
                              view_z);

            Float4 packed_normal;
            packed_normal = texture(s_gb_normal, uv);
            Float3 n_world;
            n_world = normalize(packed_normal->xyz() * Float(2.0f) - Float3(1.0f, 1.0f, 1.0f));
            Float3 n;
            n = normalize(Float3(dot(u_view_r0->xyz(), n_world),
                                 dot(u_view_r1->xyz(), n_world),
                                 dot(u_view_r2->xyz(), n_world)));
            Float roughness;
            roughness = packed_normal->w;

            Float3 v;
            v = normalize(view_pos);
            constexpr int kSteps = 80; // 对齐 GLSL 默认 ssr_steps(example_sponza.cpp:112)
            Float step_len;
            step_len = u_ssr0->z / Float(float(kSteps));
            Float3 ray_step;
            ray_step = (v - n * (dot(n, v) * Float(2.0f))) * step_len; // reflect(v, n) * step
            Float thickness;
            thickness = u_ssr0->w;

            // interleaved gradient noise 抖动。
            // $IF 的条件是**裸 C++ bool** → 走剪枝重载:关闭抖动时整段 IGN 计算不会
            // 出现在最终 shader 里(而不是算完乘 0)。必须配 $ELSE,否则分支体会被
            // 静默丢弃并在 getCurrentConditionInfo() 抛 bad_function_call。
            // 分支内只赋值给块外已声明的局部量 initial_offset。
            Float initial_offset;
            initial_offset = Float(1.0f);
            $IF(prune.ssr_jitter)
            {
                auto ign = fract(Float(52.9829189f) *
                                 fract(dot(uv * Float2(float(spz_width), float(spz_height)) +
                                               Float2(314.0f, 159.0f) * u_ssr1->w,
                                           Float2(0.06711056f, 0.00583715f))));
                initial_offset = Float(0.5f) + ign;
            }
            $ELSE
            {
                initial_offset = Float(1.0f);
            };

            Float3 ray_origin;
            ray_origin = view_pos + n * max(thickness * Float(0.35f), step_len * Float(0.12f));

            Float3 sample_pos;
            sample_pos = ray_origin + ray_step * initial_offset;
            Float3 prev_pos;
            prev_pos = ray_origin;
            Float prev_delta;
            prev_delta = Float(-1.0f);
            Float found;
            found = Float(0.0f);
            Float alive;
            alive = Float(1.0f);
            Float3 hit_lo;
            hit_lo = ray_origin;
            Float3 hit_hi;
            hit_hi = sample_pos;
            Float hit_step;
            hit_step = Float(float(kSteps));

            for (int i = 0; i < kSteps; ++i)
            {
                auto frontal = step(0.01f, sample_pos->z);
                Float2 s_uv;
                s_uv = view_to_uv(sample_pos);
                auto inx = step(0.0f, s_uv->x) * step(s_uv->x, Float(1.0f));
                auto iny = step(0.0f, s_uv->y) * step(s_uv->y, Float(1.0f));
                alive = alive * frontal * inx * iny;

                Float2 suv_c;
                suv_c = min(max(s_uv, Float2(0.f, 0.f)), Float2(1.f, 1.f));
                Float delta;
                delta = sample_pos->z - texture(s_ssr_lin, suv_c)->x;

                auto crossed = step(prev_delta, Float(1e-4f)) * step(1e-4f, delta);
                const float allow_close = (i > 0) ? 1.0f : 0.0f;
                auto close_hit = Float(allow_close) * step(1e-4f, delta) * step(delta, thickness);
                Float hit_now;
                hit_now = min(crossed + close_hit, Float(1.0f)) * alive * (Float(1.0f) - found);

                hit_lo = mix(hit_lo, prev_pos, hit_now);
                hit_hi = mix(hit_hi, sample_pos, hit_now);
                hit_step = mix(hit_step, Float(float(i)), hit_now);
                found = max(found, hit_now);

                prev_pos = sample_pos;
                prev_delta = delta;
                sample_pos = sample_pos + ray_step;
            }

            // binary search 精修(6 次,对齐 GLSL 默认 ssr_refine_steps;mid 先落变量)
            Float3 lo;
            lo = hit_lo;
            Float3 hi;
            hi = hit_hi;
            for (int k = 0; k < 6; ++k)
            {
                Float3 mid;
                mid = (lo + hi) * Float(0.5f);
                Float2 mid_uv;
                mid_uv = min(max(view_to_uv(mid), Float2(0.f, 0.f)), Float2(1.f, 1.f));
                Float cond;
                cond = step(texture(s_ssr_lin, mid_uv)->x, mid->z); // mid 在表面之后 → hi=mid
                hi = mix(hi, mid, cond);
                lo = mix(lo, mid, Float(1.0f) - cond);
            }
            Float2 hit_uv;
            hit_uv = min(max(view_to_uv(hi), Float2(0.f, 0.f)), Float2(1.f, 1.f));
            auto keep = step(hi->z - texture(s_ssr_lin, hit_uv)->x, thickness);

            Float3 refl_color;
            refl_color = texture(s_hdr, hit_uv)->xyz();

            auto ndotv2 = clamp(dot(n, -v), 0.f, 1.f);
            auto fresnel = surface_f0 + (Float(1.0f) - surface_f0) * pow(Float(1.0f) - ndotv2, u_ssr1->y);

            auto edge_fade = smoothstep_f(Float(0.0f), Float(0.12f), hit_uv->x) *
                             smoothstep_f(Float(0.0f), Float(0.12f), hit_uv->y) *
                             (Float(1.0f) - smoothstep_f(Float(0.88f), Float(1.0f), hit_uv->x)) *
                             (Float(1.0f) - smoothstep_f(Float(0.88f), Float(1.0f), hit_uv->y));

            auto dist_fade = Float(1.0f) - smoothstep_f(Float(0.35f), Float(1.0f), hit_step / Float(float(kSteps))) * Float(0.65f);
            auto rough_fade = Float(1.0f) - smoothstep_f(Float(0.08f), Float(0.50f), roughness);

            out_weight = clamp(fresnel * edge_fade * dist_fade * rough_fade * found * keep, 0.f, 1.f);
            out_color = refl_color->xyz();
        };

        ssr_trace_out << Float4(out_color, out_weight);
    };

    // 合成 + bloom(13 taps)+ ACES + 调色 + vignette
    auto aces = [&](Float3 rgb) {
        Float3 x;
        x = max(rgb, Float3(0.f, 0.f, 0.f));
        auto num = x * (x * Float(2.51f) + Float3(0.03f, 0.03f, 0.03f));
        auto den = x * (x * Float(2.43f) + Float3(0.59f, 0.59f, 0.59f)) + Float3(0.14f, 0.14f, 0.14f);
        auto tone = min(max(num / den, Float3(0.f, 0.f, 0.f)), Float3(1.f, 1.f, 1.f));
        auto g = 1.0f / 2.2f;
        return pow(tone, Float3(g, g, g));
    };

    auto luminance_of = [&](Float3 c) { return dot(c, Float3(0.2126f, 0.7152f, 0.0722f)); };

    auto bloom_tap = [&](Float2 uv) {
        Float3 c;
        c = max(texture(s_hdr, min(max(uv, Float2(0.f, 0.f)), Float2(1.f, 1.f)))->xyz(),
                Float3(0.f, 0.f, 0.f));
        Float v;
        v = luminance_of(c);
        Float threshold;
        threshold = u_post0->x;
        Float knee;
        knee = max(threshold * Float(0.5f), Float(1e-4f));
        Float soft2;
        soft2 = min(clamp(v - threshold + knee, 0.f, 1e6f), knee * Float(2.0f));
        auto contribution = max(v - threshold, Float(0.0f)) +
                            soft2 * soft2 / (knee * Float(4.0f) + Float(1e-4f));
        return c * (contribution / max(v, Float(1e-4f)));
    };

    auto ssr_composite_frag = [&](Aggregate<EdslQuadVertOut> in) {
        Float2 uv;
        uv = in->v_uv;
        Float4 base;
        base = texture(s_hdr, uv);
        Float4 ssr;
        ssr = texture(s_ssr_buf, uv);

        auto weight = clamp(ssr->w * u_ssr1->x, 0.f, 1.f);
        auto mixed = mix(Float3(base->xyz()), Float3(ssr->xyz()), weight);

        // bloom:中心 + 4 邻 + 4 斜 + 4 远(权重同 GLSL 版)
        Float2 bloom_step;
        bloom_step = u_texel->xy() * u_post0->z;
        Float3 bloom;
        bloom = bloom_tap(uv) * Float(0.20f);
        const float r_taps[12][3] = {
            { 1, 0, 0.09f }, { -1, 0, 0.09f }, { 0, 1, 0.09f }, { 0, -1, 0.09f },
            { 1, 1, 0.06f }, { -1, 1, 0.06f }, { 1, -1, 0.06f }, { -1, -1, 0.06f },
            { 2, 0, 0.05f }, { -2, 0, 0.05f }, { 0, 2, 0.05f }, { 0, -2, 0.05f },
        };
        for (const auto& tap : r_taps)
        {
            Float2 ouv;
            ouv = uv + Float2(tap[0], tap[1]) * bloom_step;
            bloom = bloom + bloom_tap(ouv) * Float(tap[2]);
        }
        auto with_bloom = mixed + bloom * u_post0->y;

        Float3 toned;
        toned = aces(with_bloom * u_post1->x);

        // 调色:色温 → 饱和度 → 对比度
        Float temp;
        temp = u_post1->w;
        Float3 graded;
        graded = Float3(toned->x * (Float(1.0f) + temp * Float(0.12f)),
                        toned->y,
                        toned->z * (Float(1.0f) - temp * Float(0.12f)));
        Float luma;
        luma = luminance_of(graded);
        auto sat = mix(Float3(luma, luma, luma), graded, u_post1->z);
        auto contrasted = min(max((sat - Float3(0.5f, 0.5f, 0.5f)) * u_post1->y + Float3(0.5f, 0.5f, 0.5f),
                                  Float3(0.f, 0.f, 0.f)), Float3(1.f, 1.f, 1.f));

        // vignette
        Float2 centered;
        centered = uv * Float(2.0f) - Float2(1.0f, 1.0f);
        Float cx;
        cx = centered->x * Float(float(spz_width) / float(spz_height));
        auto dist = sqrt(cx * cx + centered->y * centered->y);
        auto vig = Float(1.0f) - smoothstep_f(Float(0.35f), Float(1.35f), dist) * u_post0->w;

        final_out << Float4(contrasted * vig, 1.f);
    };

    // ========================================================================
    // 管线
    // ========================================================================

    horizon::RasterizerPipelineDesc geo_desc; // 混合状态一份即可,运行时复制到全部附件
    geo_desc.blend_enabled = false;

    horizon::RasterizerPipelineDesc fullscreen_desc;
    fullscreen_desc.blend_enabled = false;
    fullscreen_desc.depth_test_enabled = false;
    fullscreen_desc.depth_write_enabled = false;

    horizon::RasterizerPipeline rsm_pipe(rsm_vert, rsm_frag, geo_desc);
    rsm_pipe.bind_depth_target(rsm_zbuffer);

    horizon::RasterizerPipeline gb_pipe(gb_vert, gb_frag, geo_desc);
    gb_pipe.bind_depth_target(gbuffer_zbuffer);

    horizon::RasterizerPipeline ssao_pipe(quad_vert, ssao_frag, fullscreen_desc);
    horizon::RasterizerPipeline ssao_blur_pipe(quad_vert, ssao_blur_frag, fullscreen_desc);
    horizon::RasterizerPipeline light_pipe(quad_vert, light_frag, fullscreen_desc);
    horizon::RasterizerPipeline combine_pipe(quad_vert, combine_frag, fullscreen_desc);
    horizon::RasterizerPipeline ssr_lin_pipe(quad_vert, ssr_lin_frag, fullscreen_desc);
    horizon::RasterizerPipeline ssr_trace_pipe(quad_vert, ssr_trace_frag, fullscreen_desc);
    horizon::RasterizerPipeline ssr_composite_pipe(quad_vert, ssr_composite_frag, fullscreen_desc);

    horizon::HardwareExecutor render_executor;
    horizon::HardwareExecutor display_executor;
    horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    horizon::DrawIndexedParams quad_params;
    quad_params.index_count = static_cast<uint32_t>(corner_indices.size());

    // ---- 相机 / 太阳 / 场景尺度 ----
    const glm::vec3 scene_min = to_vec3(asset.scene.min);
    const glm::vec3 scene_max = to_vec3(asset.scene.max);
    const glm::vec3 scene_center = (scene_min + scene_max) * 0.5f;
    const glm::vec3 scene_extent = scene_max - scene_min;

    FlyCamera camera;
    camera.position = to_vec3(asset.scene.camera_position);
    const float main_floor_y =
        asset.version == hzms_version_pbr ? 0.0f : camera.position.y - scene_extent.y * 0.10f;
    const float hall_center_z =
        asset.version == hzms_version_pbr ? 0.0f : camera.position.z;
    if (asset.version == hzms_version_pbr)
    {
        // 与 GLSL 版相同的定妆机位(live session 捕获)
        camera.position = glm::vec3(13.3404408f, 1.889889f, -0.84720045f);
        camera.yaw = 10.9808016f;
        camera.pitch = 0.07695994f;
    }
    else
    {
        const glm::vec3 dir = glm::normalize(to_vec3(asset.scene.camera_target) - camera.position);
        camera.pitch = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
        camera.yaw = std::atan2(dir.x, dir.z);
    }
    camera.speed = glm::length(scene_extent) * 0.12f;
    const FlyCamera default_camera = camera;

    constexpr float aspect = static_cast<float>(spz_width) / static_cast<float>(spz_height);
    const float z_far = glm::length(scene_extent) * 2.0f;
    const glm::mat4 proj = [&] {
        glm::mat4 m = glm::perspectiveLH(asset.scene.camera_yfov, aspect, asset.scene.camera_znear, z_far);
        m[1][1] *= -1.0f;
        return m;
    }();

    const float scene_radius = glm::length(scene_extent) * 0.5f;
    const auto build_sun_view_proj = [&](const glm::vec3& sun_direction) {
        const glm::vec3 up = std::abs(sun_direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                               : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 eye = scene_center - sun_direction * scene_radius * 1.5f;
        const glm::mat4 light_view = glm::lookAtLH(eye, scene_center, up);
        glm::mat4 light_proj = glm::orthoLH(-scene_radius, scene_radius,
                                            -scene_radius, scene_radius,
                                            0.0f, scene_radius * 3.0f);
        light_proj[1][1] *= -1.0f;
        return light_proj * light_view;
    };
    const float shadow_texel_world = (2.0f * scene_radius) / static_cast<float>(spz_shadow_map_size);

    const float p00 = proj[0][0];
    const float p11 = proj[1][1];
    const float p22 = proj[2][2];
    const float p32 = proj[3][2];

    // ---- SSR 演示镜:中殿反射池(同 GLSL 版;斜立镜已删——挡构图且主要
    // 反射屏幕外内容,不适合 SSR 展示)----
    const float mirror_diag = glm::length(scene_extent);
    const float mirror_y = main_floor_y + mirror_diag * 0.00045f;
    const glm::vec3 pool_center(scene_min.x + scene_extent.x * 0.58f, mirror_y, hall_center_z);

    std::vector<SponzaVertex> mirror_vertices;
    std::vector<uint16_t> mirror_indices;
    const auto make_panel = [&](const glm::vec3& center, const glm::vec3& normal_dir, float width, float height) {
        const glm::vec3 n = glm::normalize(normal_dir);
        const glm::vec3 helper_up = std::abs(n.y) > 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                         : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 right = glm::normalize(glm::cross(helper_up, n));
        const glm::vec3 v_up = glm::normalize(glm::cross(n, right));
        const uint16_t base = static_cast<uint16_t>(mirror_vertices.size());
        const glm::vec3 corners[4] = {
            center - right * (width * 0.5f) - v_up * (height * 0.5f),
            center + right * (width * 0.5f) - v_up * (height * 0.5f),
            center - right * (width * 0.5f) + v_up * (height * 0.5f),
            center + right * (width * 0.5f) + v_up * (height * 0.5f),
        };
        const float uvs[4][2] = { { 0, 1 }, { 1, 1 }, { 0, 0 }, { 1, 0 } };
        for (int i = 0; i < 4; ++i)
        {
            SponzaVertex v {};
            v.position = { corners[i].x, corners[i].y, corners[i].z };
            v.normal = { n.x, n.y, n.z };
            v.tangent = { right.x, right.y, right.z, 1.0f };
            v.uv = { uvs[i][0], uvs[i][1] };
            mirror_vertices.push_back(v);
        }
        for (uint32_t idx : { 0u, 1u, 2u, 1u, 3u, 2u })
            mirror_indices.push_back(static_cast<uint16_t>(base + idx));
    };
    // 0: 镜面;1: 略低且略大的深石围边。
    make_panel(pool_center, glm::vec3(0.0f, 1.0f, 0.0f),
               scene_extent.x * 0.19f, scene_extent.z * 0.105f);
    make_panel(pool_center - glm::vec3(0.0f, mirror_diag * 0.00025f, 0.0f),
               glm::vec3(0.0f, 1.0f, 0.0f),
               scene_extent.x * 0.215f, scene_extent.z * 0.135f);
    struct MirrorRange { uint32_t first; uint32_t count; };
    const MirrorRange mirror_pool_range { 0, 6 };
    const MirrorRange mirror_frame_range { 6, 6 };

    horizon::HardwareBuffer mirror_vb =
        horizon::HardwareBuffer::vertex(mirror_vertices, "edsl_sponza.mirror.vb");
    horizon::HardwareBuffer mirror_ib =
        horizon::HardwareBuffer::index(mirror_indices, "edsl_sponza.mirror.ib");

    HorizonImGuiLayer ui(window, spz_width, spz_height);

    // tuning 已在 shader lambda 之前声明(剪枝 detector 需要),这里只做资产相关的重标定。
    const float scene_diag = glm::length(scene_extent);
    if (asset.version != hzms_version_pbr)
    {
        tuning.ssr_max_distance = scene_diag * 0.55f;
        tuning.ssr_thickness = scene_diag * 0.0035f;
        tuning.ssao_radius = scene_diag * 0.015f;
        tuning.ssao_bias = scene_diag * 0.00025f;
        tuning.ssao_blur_depth_sigma = scene_diag * 0.006f;
    }
    const float rsm_clamp_d2 = (scene_radius * 0.02f) * (scene_radius * 0.02f);

    const auto start_time = std::chrono::high_resolution_clock::now();
    auto prev_time = start_time;
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;
    uint32_t frame_index = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ui.new_frame();
        ImGui::SetNextWindowSize(ImVec2(430.0f, 620.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Sponza EDSL");
        ImGui::Text("RMB drag: look   WASD: move   Q/E: down/up   Shift: fast");
        ImGui::Text("submeshes %zu", asset.submeshes.size());
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Sun (daylight)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("elevation", &tuning.sun_elevation_deg, 5.0f, 89.0f);
            ImGui::SliderFloat("azimuth", &tuning.sun_azimuth_deg, -180.0f, 180.0f);
            ImGui::SliderFloat("sun intensity", &tuning.sun_intensity, 0.0f, 10.0f);
            ImGui::ColorEdit3("sun colour", &tuning.sun_color.x);
            ImGui::Checkbox("cast shadows", &tuning.sun_shadow);
            ImGui::SliderFloat("shadow bias", &tuning.shadow_bias, 0.0f, 0.01f, "%.4f");
        }
        if (ImGui::CollapsingHeader("RSM indirect", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("enable##rsm", &tuning.rsm_enable);
            ImGui::SliderFloat("intensity##rsm", &tuning.rsm_intensity, 0.0f, 30.0f);
            ImGui::SliderFloat("sample radius (uv)", &tuning.rsm_radius, 0.02f, 0.5f);
        }
        if (ImGui::CollapsingHeader("Disney BRDF"))
        {
            ImGui::SliderFloat("specular", &tuning.disney_specular, 0.0f, 1.0f);
            ImGui::SliderFloat("specular tint", &tuning.disney_specular_tint, 0.0f, 1.0f);
            ImGui::SliderFloat("sheen", &tuning.disney_sheen, 0.0f, 1.0f);
            ImGui::SliderFloat("sheen tint", &tuning.disney_sheen_tint, 0.0f, 1.0f);
            ImGui::SliderFloat("clearcoat", &tuning.disney_clearcoat, 0.0f, 1.0f);
            ImGui::SliderFloat("clearcoat gloss", &tuning.disney_clearcoat_gloss, 0.0f, 1.0f);
            ImGui::SliderFloat("subsurface", &tuning.disney_subsurface, 0.0f, 1.0f);
            ImGui::SliderFloat("anisotropic", &tuning.disney_anisotropic, 0.0f, 1.0f);
        }
        if (ImGui::CollapsingHeader("SSR", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("enable##ssr", &tuning.ssr_enable);
            ImGui::Checkbox("show mirrors", &tuning.mirror_enable);
            ImGui::SliderFloat("mirror roughness", &tuning.mirror_roughness, 0.0f, 0.5f);
            ImGui::SliderFloat("intensity##ssr", &tuning.ssr_intensity, 0.0f, 2.0f);
            ImGui::SliderFloat("max distance", &tuning.ssr_max_distance, scene_diag * 0.02f, scene_diag * 1.5f);
            ImGui::SliderFloat("thickness", &tuning.ssr_thickness, scene_diag * 0.0005f, scene_diag * 0.05f);
            ImGui::Checkbox("jitter", &tuning.ssr_jitter);
            ImGui::SliderFloat("fresnel power", &tuning.ssr_fresnel_power, 1.0f, 8.0f);
        }
        if (ImGui::CollapsingHeader("Indirect light (IBL)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("irradiance", &tuning.irradiance_scale, 0.0f, 2.0f);
            ImGui::SliderFloat("radiance", &tuning.radiance_scale, 0.0f, 2.0f);
            ImGui::SliderFloat("environment rotation", &tuning.env_rotation_deg, -180.0f, 180.0f);
            ImGui::ColorEdit3("ambient floor", &tuning.ambient_floor.x);
            ImGui::SliderFloat("AO strength", &tuning.ssao_intensity, 0.0f, 2.0f);
            ImGui::SliderFloat("AO radius", &tuning.ssao_radius, scene_diag * 0.001f, scene_diag * 0.06f);
            ImGui::SliderFloat("AO bias", &tuning.ssao_bias, 0.0f, scene_diag * 0.003f);
        }
        if (ImGui::CollapsingHeader("Output"))
        {
            ImGui::SliderFloat("exposure", &tuning.exposure, 0.1f, 4.0f);
            ImGui::SliderFloat("bloom intensity", &tuning.bloom_intensity, 0.0f, 0.35f);
            ImGui::SliderFloat("bloom threshold", &tuning.bloom_threshold, 0.1f, 4.0f);
            ImGui::SliderFloat("bloom radius", &tuning.bloom_radius, 1.0f, 8.0f);
            ImGui::SliderFloat("contrast", &tuning.output_contrast, 0.8f, 1.25f);
            ImGui::SliderFloat("saturation", &tuning.output_saturation, 0.0f, 1.35f);
            ImGui::SliderFloat("temperature", &tuning.output_temperature, -0.25f, 0.25f);
            ImGui::SliderFloat("vignette", &tuning.vignette, 0.0f, 0.35f);
        }
        if (ImGui::Button("Reset to defaults"))
        {
            Tuning defaults;
            if (asset.version != hzms_version_pbr)
            {
                defaults.ssr_max_distance = scene_diag * 0.55f;
                defaults.ssr_thickness = scene_diag * 0.0035f;
                defaults.ssao_radius = scene_diag * 0.015f;
                defaults.ssao_bias = scene_diag * 0.00025f;
                defaults.ssao_blur_depth_sigma = scene_diag * 0.006f;
            }
            tuning = defaults;
            camera = default_camera;
        }
        ImGui::End();

        const auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - prev_time).count();
        prev_time = now;

        update_camera(window, camera, std::min(dt, 0.1f));

        const glm::mat4 view = camera.view();
        const glm::mat4 view_proj = proj * view;
        const glm::mat4 inv_view_proj = glm::inverse(view_proj);

        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[160];
            std::snprintf(title, sizeof(title), "Horizon Sponza [EDSL] - %zu submeshes - %.1f FPS (%.2f ms)",
                          asset.submeshes.size(), fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        const float sun_el = glm::radians(tuning.sun_elevation_deg);
        const float sun_az = glm::radians(tuning.sun_azimuth_deg);
        const glm::vec3 sun_direction = glm::normalize(glm::vec3(
            std::cos(sun_el) * std::sin(sun_az), -std::sin(sun_el), std::cos(sun_el) * std::cos(sun_az)));
        const glm::mat4 sun_view_proj = build_sun_view_proj(sun_direction);
        const glm::mat4 inv_sun_view_proj = glm::inverse(sun_view_proj);

        // ---- uniforms ----
        rsm_light_view_proj = to_edsl_matrix(sun_view_proj);
        gb_view_proj = to_edsl_matrix(view_proj);

        u_inv_view_proj = to_edsl_matrix(inv_view_proj);
        u_view = to_edsl_matrix(view);
        u_proj_view = to_edsl_matrix(view_proj);
        u_sun_view_proj = to_edsl_matrix(sun_view_proj);
        // 同步编译期剪枝开关。必须在 record 之前:ConditionInfo 在 build_draw_plan()
        // 里每帧每管线求值一次,值变了就会切到另一个 shader 变体(首次切换要付一次
        // Slang link + 建管线的开销,之后命中 pipeline_pool_)。
        prune.sun_shadow = tuning.sun_shadow;
        prune.rsm = tuning.rsm_enable;
        prune.ssr = tuning.ssr_enable;
        prune.ssr_jitter = tuning.ssr_jitter;
        prune.disney_extra = tuning.disney_sheen > 0.0f || tuning.disney_clearcoat > 0.0f ||
                             tuning.disney_subsurface > 0.0f || tuning.disney_anisotropic > 0.0f;

        u_inv_sun_view_proj = to_edsl_matrix(inv_sun_view_proj);
        u_camera_pos = to_edsl_vec4(glm::vec4(camera.position, 1.0f));
        u_sun_dir_ws = to_edsl_vec4(glm::vec4(sun_direction, 0.0f));
        u_sun_radiance = to_edsl_vec4(glm::vec4(tuning.sun_color * tuning.sun_intensity, 0.0f));
        u_shadow_params = to_edsl_vec4(glm::vec4(shadow_texel_world, tuning.shadow_bias,
                                                 1.0f / float(spz_shadow_map_size),
                                                 tuning.sun_shadow ? 1.0f : 0.0f));
        u_rsm_params = to_edsl_vec4(glm::vec4(tuning.rsm_radius, tuning.rsm_intensity,
                                              tuning.rsm_enable ? 1.0f : 0.0f, rsm_clamp_d2));
        u_disney0 = to_edsl_vec4(glm::vec4(tuning.disney_specular, tuning.disney_specular_tint,
                                           tuning.disney_sheen, tuning.disney_sheen_tint));
        u_disney1 = to_edsl_vec4(glm::vec4(tuning.disney_clearcoat, tuning.disney_clearcoat_gloss,
                                           tuning.disney_subsurface, tuning.disney_anisotropic));
        {
            const float cc_a = 0.1f + (0.001f - 0.1f) * tuning.disney_clearcoat_gloss;
            const float cc_a2 = cc_a * cc_a;
            u_cc = to_edsl_vec4(glm::vec4(cc_a2, std::log(cc_a2), 0.0f, 0.0f));
        }
        u_ssao_params = to_edsl_vec4(glm::vec4(tuning.ssao_radius, tuning.ssao_bias, tuning.ssao_intensity, 0.0f));
        u_ssao_blur = to_edsl_vec4(glm::vec4(1.0f / spz_width, 1.0f / spz_height,
                                             tuning.ssao_blur_depth_sigma, 16.0f));
        u_depth_unpack = to_edsl_vec4(glm::vec4(p32, -p22, z_far, 0.0f));
        u_env_params = to_edsl_vec4(glm::vec4(tuning.irradiance_scale, tuning.radiance_scale,
                                              float(radiance_dds.mip_count),
                                              glm::radians(tuning.env_rotation_deg)));
        u_ambient = to_edsl_vec4(glm::vec4(tuning.ambient_floor, 0.0f));
        u_ssr0 = to_edsl_vec4(glm::vec4(p00, p11, tuning.ssr_max_distance, tuning.ssr_thickness));
        u_ssr1 = to_edsl_vec4(glm::vec4(tuning.ssr_enable ? tuning.ssr_intensity : 0.0f,
                                        tuning.ssr_fresnel_power,
                                        tuning.ssr_jitter ? 1.0f : 0.0f,
                                        float(frame_index & 63u)));
        u_ndc_to_view = to_edsl_vec4(glm::vec4(2.0f / p00, 2.0f / p11, -1.0f / p00, -1.0f / p11));
        u_view_r0 = to_edsl_vec4(glm::vec4(view[0][0], view[1][0], view[2][0], 0.0f));
        u_view_r1 = to_edsl_vec4(glm::vec4(view[0][1], view[1][1], view[2][1], 0.0f));
        u_view_r2 = to_edsl_vec4(glm::vec4(view[0][2], view[1][2], view[2][2], 0.0f));
        u_post0 = to_edsl_vec4(glm::vec4(tuning.bloom_threshold, tuning.bloom_intensity,
                                         tuning.bloom_radius, tuning.vignette));
        u_post1 = to_edsl_vec4(glm::vec4(tuning.exposure, tuning.output_contrast,
                                         tuning.output_saturation, tuning.output_temperature));
        u_texel = to_edsl_vec4(glm::vec4(1.0f / spz_width, 1.0f / spz_height, 0.0f, 0.0f));

        // ---- 几何 pass 录制(RSM ×3 + G-buffer ×3;逐 draw 重绑材质贴图)----
        rsm_pipe.clear_records();
        gb_pipe.clear_records();

        const auto record_geometry = [&](horizon::HardwareBuffer& ib, horizon::HardwareBuffer& vb,
                                         uint32_t first_index, uint32_t index_count,
                                         int32_t vertex_offset = 0) {
            horizon::DrawIndexedParams params;
            params.index_count = index_count;
            params.first_index = first_index;
            params.vertex_offset = vertex_offset;
            rsm_pipe.record(ib, vb, params);
            gb_pipe.record(ib, vb, params);
        };

        for (size_t submesh_index = 0; submesh_index < asset.submeshes.size(); ++submesh_index)
        {
            const SponzaSubmesh& submesh = asset.submeshes[submesh_index];
            if (submesh.material < 0 || static_cast<size_t>(submesh.material) >= asset.materials.size())
                continue;
            const SponzaMaterial& material = asset.materials[static_cast<size_t>(submesh.material)];

            auto& base_tex = texture_or(material.base_color_texture, white_texture);
            auto& normal_tex = texture_or(material.normal_texture, flat_normal_texture);
            auto& mr_tex = texture_or(material.metal_rough_texture, white_texture);

            rsm_base_color_tex = base_tex;
            gb_base_color_tex = base_tex;
            gb_normal_tex = normal_tex;
            gb_mr_tex = mr_tex;

            const glm::vec4 factor(material.base_color_factor[0], material.base_color_factor[1],
                                   material.base_color_factor[2], material.base_color_factor[3]);
            rsm_albedo_factor = to_edsl_vec4(factor);
            gb_base_factor = to_edsl_vec4(factor);
            gb_mr_factor = to_edsl_vec4(glm::vec4(material.metallic, material.roughness, 0.0f, 0.0f));

            for (const SponzaDrawRange& range : submesh_ranges[submesh_index])
                record_geometry(scene_ib, scene_vb, range.first_index, range.index_count,
                                range.vertex_offset);
        }

        if (tuning.mirror_enable)
        {
            const auto record_mirror = [&](const MirrorRange& range, const glm::vec4& color,
                                           float metallic, float roughness, const glm::vec4& rsm_color) {
                rsm_base_color_tex = white_texture;
                gb_base_color_tex = white_texture;
                gb_normal_tex = flat_normal_texture;
                gb_mr_tex = white_texture;
                rsm_albedo_factor = to_edsl_vec4(rsm_color);
                gb_base_factor = to_edsl_vec4(color);
                gb_mr_factor = to_edsl_vec4(glm::vec4(metallic, roughness, 0.0f, 0.0f));
                record_geometry(mirror_ib, mirror_vb, range.first, range.count);
            };
            const glm::vec4 mirror_rsm_dark(0.12f, 0.12f, 0.12f, 1.0f);
            record_mirror(mirror_pool_range, glm::vec4(0.93f, 0.95f, 0.97f, 1.0f), 1.0f, tuning.mirror_roughness, mirror_rsm_dark);
            record_mirror(mirror_frame_range, glm::vec4(0.055f, 0.045f, 0.038f, 1.0f), 0.0f, 0.72f, mirror_rsm_dark);
        }

        // ---- 全屏 pass 录制 ----
        ssao_pipe.clear_records();
        ssao_pipe.record(quad_ib, quad_vb, quad_params);
        ssao_blur_pipe.clear_records();
        ssao_blur_pipe.record(quad_ib, quad_vb, quad_params);
        light_pipe.clear_records();
        light_pipe.record(quad_ib, quad_vb, quad_params);
        combine_pipe.clear_records();
        combine_pipe.record(quad_ib, quad_vb, quad_params);
        ssr_lin_pipe.clear_records();
        ssr_lin_pipe.record(quad_ib, quad_vb, quad_params);
        ssr_trace_pipe.clear_records();
        ssr_trace_pipe.record(quad_ib, quad_vb, quad_params);
        ssr_composite_pipe.clear_records();
        ssr_composite_pipe.record(quad_ib, quad_vb, quad_params);

        horizon::SubmitReceipt render_receipt =
            render_executor << rsm_pipe.extent(spz_shadow_map_size, spz_shadow_map_size)
                            << gb_pipe.extent(spz_width, spz_height)
                            << ssao_pipe.extent(spz_width, spz_height)
                            << ssao_blur_pipe.extent(spz_width, spz_height)
                            << light_pipe.extent(spz_width, spz_height)
                            << combine_pipe.extent(spz_width, spz_height)
                            << ssr_lin_pipe.extent(spz_width, spz_height)
                            << ssr_trace_pipe.extent(spz_width, spz_height)
                            << ssr_composite_pipe.extent(spz_width, spz_height)
                            << horizon::commit();

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << horizon::present(display, final_output_image)
                                         << horizon::commit());
        ++frame_index;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
