// Deferred shading over the Khronos Sponza scene, following the three-pass
// structure of example_deferred (G-buffer MRT -> additive light accumulation ->
// combine).
//
// Assets come from tools/sponza/convert_sponza.py, which turns the upstream
// glTF + ASTC/UASTC data into assets/sponza/{sponza.bin, textures/*.png}. See
// tools/sponza/README.md for the container layout and the caveats.
//
// Differences from example_deferred, all driven by the scene rather than by the
// technique:
// - geometry is one shared vertex/index buffer sliced into 25 submeshes, so the
//   G-buffer pass rebinds only a push constant per draw and the node transforms
//   are already baked into the vertices (no per-draw model matrix);
// - materials carry a base colour plus optional height/specular/mask textures,
//   all resident in the bindless table at once;
// - the light pass also applies the scene's authored directional sun, using a
//   full-screen rect and the negative-radius branch in sponza_light_frag.glsl;
// - a free-fly camera replaces the fixed viewpoint, because Sponza is a
//   walkthrough scene.

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/sponza_shadow_vert.glsl)
#include GLSL(shaders/sponza_shadow_frag.glsl)
#include GLSL(shaders/sponza_geom_vert.glsl)
#include GLSL(shaders/sponza_geom_frag.glsl)
#include GLSL(shaders/sponza_light_vert.glsl)
#include GLSL(shaders/sponza_light_frag.glsl)
#include GLSL(shaders/sponza_combine_vert.glsl)
#include GLSL(shaders/sponza_combine_frag.glsl)
#include GLSL(shaders/sponza_ssao_frag.glsl)
#include GLSL(shaders/sponza_ssao_blur_frag.glsl)

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

namespace
{
constexpr uint32_t spz_width = 1280;
constexpr uint32_t spz_height = 720;
constexpr int spz_max_lights = 128;
constexpr uint32_t spz_shadow_map_size = 2048;

// Everything the ImGui panel can drive. The defaults are the calibrated values:
// see tools/sponza/README.md for why they sit where they do -- in short, the
// scene is ~3700 units across, accumulation is linear and tone mapped, and a
// peak accumulated diffuse near 1.0 is what the filmic curve resolves best.
struct Tuning
{
    // --- direct light ---
    float sun_intensity = 0.40f;
    glm::vec3 sun_color { 1.0f, 0.96f, 0.88f };
    bool sun_shadow = true;
    float shadow_bias = 0.0015f;

    int point_light_count = 48;
    float point_intensity = 0.26f;
    // A radius much larger than this makes every light cover the whole frame,
    // so the accumulation buffer saturates and the result flattens.
    float point_radius = 420.0f;
    // Fraction of the radius before falloff starts. example_deferred uses 0.8,
    // which at this scale stacks a dozen unattenuated lights on every pixel.
    float point_inner = 0.0f;
    float light_speed = 0.25f;
    bool animate_lights = true;
    float specular_strength = 0.35f;

    // --- surface response ---
    // Every glTF material in this scene shares metallicFactor 0.588 and
    // roughnessFactor 0.9, so those carry no per-material information. The
    // '*_spec' textures are the only real signal, and they drive both the
    // specular intensity and the glossiness through these ranges.
    float spec_min = 0.02f;
    float spec_max = 0.90f;
    float gloss_min = 0.05f;
    float gloss_max = 0.85f;
    // Materials with no '*_spec' map at all (the curtains, the roof, ...).
    float no_map_specular = 0.05f;
    float no_map_gloss = 0.08f;
    // Off by default: deriving metalness from the specular map does not work on
    // this scene. The '*_spec' maps encode shininess, not metalness, and the
    // shiniest surface in Sponza is the polished marble floor (mean 197/255)
    // rather than any metal -- the flagpole is 76 and the metal details 53. Any
    // threshold therefore classifies the floor as metal before it reaches a real
    // one. Left exposed so the failure is inspectable via the "specular tint"
    // debug view; a correct fix needs per-material metalness authored in the
    // converter, since the glTF metallicFactor is one constant for all 25.
    float metal_low = 0.35f;
    float metal_high = 0.85f;
    float metal_tint = 0.0f;

    // --- indirect light ---
    // The probe is an outdoor capture, so it is far brighter than this interior
    // warrants; these scale it down to a plausible bounce.
    float irradiance_scale = 0.45f;
    float radiance_scale = 0.30f;
    glm::vec3 ambient_floor { 0.045f, 0.052f, 0.070f };

    float ssao_radius = 55.0f;
    float ssao_bias = 1.5f;
    float ssao_intensity = 1.0f;
    float ssao_blur_rejection = 4000.0f;

    // --- output ---
    float exposure = 1.15f;
};
constexpr float pi_half = 1.5707963f;

const std::filesystem::path spz_asset_root =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "sponza";

// ============================================================================
// HZMS container (see tools/sponza/README.md)
// ============================================================================

constexpr uint32_t hzms_magic = 0x534D5A48; // 'HZMS'
constexpr uint32_t hzms_version = 2;

// Flags stored in the HZMS material record by the converter.
constexpr uint32_t hzms_material_double_sided = 1u << 0;
constexpr uint32_t hzms_material_alpha_mask = 1u << 1;

// Flags passed to sponza_geom_frag.glsl. These are a different set from the
// asset-side flags above: bit0 means "sample the normal map", not "double sided".
constexpr uint32_t shader_material_has_normal = 1u << 0;
constexpr uint32_t shader_material_alpha_mask = 1u << 1;
constexpr uint32_t shader_material_has_specular = 1u << 2;

// Must match the vertex layout the converter writes: 48 bytes, and the input
// declarations in sponza_geom_vert.glsl.
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
static_assert(sizeof(SponzaSubmesh) == 40, "SponzaSubmesh must match the HZMS submesh record");

struct SponzaMaterial
{
    int32_t base_color_texture;
    int32_t bump_texture;
    int32_t specular_texture;
    int32_t mask_texture;
    std::array<float, 4> base_color_factor;
    float metallic;
    float roughness;
    uint32_t name_offset;
    uint32_t flags;
};
static_assert(sizeof(SponzaMaterial) == 48, "SponzaMaterial must match the HZMS material record");

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
static_assert(sizeof(SponzaScene) == 25 * sizeof(float), "SponzaScene must match the HZMS scene block");

struct SponzaAsset
{
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
    if (version != hzms_version)
        throw std::runtime_error("HZMS version " + std::to_string(version) +
                                 " is not supported, expected " + std::to_string(hzms_version));

    const uint32_t vertex_count = read_pod<uint32_t>(bytes, cursor);
    const uint32_t index_count = read_pod<uint32_t>(bytes, cursor);
    const uint32_t submesh_count = read_pod<uint32_t>(bytes, cursor);
    const uint32_t material_count = read_pod<uint32_t>(bytes, cursor);
    const uint32_t texture_count = read_pod<uint32_t>(bytes, cursor);
    const uint32_t string_bytes = read_pod<uint32_t>(bytes, cursor);

    SponzaAsset asset;
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

    return asset;
}

// ============================================================================
// KTX1 texture loading (BC7 with a full mip chain, produced by the converter)
//
// common.cpp's loadTextureWithMipmapAndLayers only declares a mip count and
// replicates level 0, so it cannot upload a real chain. This follows the
// staging + per-mip copy_from pattern from example_deferred instead.
// ============================================================================

constexpr std::array<uint8_t, 12> ktx1_identifier = {
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
};

Corona::Horizon::Format ktx_format_from_gl(uint32_t gl_internal_format)
{
    switch (gl_internal_format)
    {
    case 0x8E8C: return Corona::Horizon::Format::BC7_UNORM;      // COMPRESSED_RGBA_BPTC_UNORM
    case 0x8E8D: return Corona::Horizon::Format::BC7_UNORM_SRGB; // COMPRESSED_SRGB_ALPHA_BPTC_UNORM
    case 0x8058: return Corona::Horizon::Format::RGBA8_UNORM;
    case 0x8C43: return Corona::Horizon::Format::SRGBA8_UNORM;
    default:
        throw std::runtime_error("Unsupported KTX internal format: " +
                                 std::to_string(gl_internal_format));
    }
}

Corona::Horizon::HardwareImage create_ktx_texture(const std::filesystem::path& path,
                                                  const std::string& name)
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

    // Strip the per-level size prefixes so the payload can be staged contiguously.
    std::vector<std::byte> payload;
    std::vector<uint64_t> level_offsets;
    payload.reserve(bytes.size());
    for (uint32_t mip = 0; mip < mip_count; ++mip)
    {
        const uint32_t image_size = read_pod<uint32_t>(bytes, cursor);
        if (cursor + image_size > bytes.size())
            throw std::runtime_error("KTX level " + std::to_string(mip) +
                                     " is truncated: " + path.string());
        level_offsets.push_back(payload.size());
        payload.insert(payload.end(), bytes.begin() + static_cast<ptrdiff_t>(cursor),
                       bytes.begin() + static_cast<ptrdiff_t>(cursor + image_size));
        cursor += image_size;
        cursor += (4 - (image_size % 4)) % 4; // mipPadding
    }

    Corona::Horizon::HardwareImageDesc desc = Corona::Horizon::HardwareImageDesc::texture_2d(
        width, height, ktx_format_from_gl(gl_internal_format),
        Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferDst,
        name);
    desc.mip_levels = mip_count;

    Corona::Horizon::HardwareImage image(desc);

    Corona::Horizon::HardwareBufferDesc staging_desc;
    staging_desc.element_count = payload.size();
    staging_desc.element_size = 1;
    staging_desc.usage = Corona::Horizon::BufferUsageFlags::TransferSrc;
    staging_desc.cpu_access = Corona::Horizon::CpuAccessMode::Write;
    Corona::Horizon::HardwareBuffer staging(staging_desc, std::span<const std::byte>(payload));

    Corona::Horizon::HardwareExecutor executor;
    Corona::Horizon::HardwareStream stream = executor.stream();
    for (uint32_t mip = 0; mip < mip_count; ++mip)
        stream << image.copy_from(staging, level_offsets[mip], 0, mip);
    (void)(stream << Corona::Horizon::commit());

    return image;
}

// ============================================================================
// DDS cubemap loading for the IBL probe (DX10 header + RGBA16F + full mip
// chain). Same layout as the loader in example_ibl, which these assets ship for.
// ============================================================================

struct CubeMapData
{
    uint32_t size = 0;
    uint32_t mip_count = 0;
    std::vector<std::byte> payload; // face-major: face0 all mips, face1 all mips, ...
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

    constexpr uint32_t dds_magic = 0x20534444;        // "DDS "
    constexpr uint32_t fourcc_dx10 = 0x30315844;      // "DX10"
    constexpr uint32_t dxgi_format_rgba16_float = 10; // DXGI_FORMAT_R16G16B16A16_FLOAT
    constexpr uint32_t caps2_cubemap = 0x200;
    constexpr size_t dx10_payload_offset = 148;       // 4 magic + 124 header + 20 DX10 header

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

Corona::Horizon::HardwareImage create_cubemap_image(const CubeMapData& dds, const std::string& name)
{
    constexpr uint32_t bytes_per_pixel = 8; // RGBA16F

    Corona::Horizon::HardwareImageDesc desc = Corona::Horizon::HardwareImageDesc::cube(
        dds.size, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferDst, name);
    desc.mip_levels = dds.mip_count;

    Corona::Horizon::HardwareImage image(desc);

    Corona::Horizon::HardwareBufferDesc staging_desc;
    staging_desc.element_count = dds.payload.size();
    staging_desc.element_size = 1;
    staging_desc.usage = Corona::Horizon::BufferUsageFlags::TransferSrc;
    staging_desc.cpu_access = Corona::Horizon::CpuAccessMode::Write;
    Corona::Horizon::HardwareBuffer staging(staging_desc, std::span<const std::byte>(dds.payload));

    Corona::Horizon::HardwareExecutor executor;
    Corona::Horizon::HardwareStream stream = executor.stream();
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
    (void)(stream << Corona::Horizon::commit());

    return image;
}

// ============================================================================
// Screen quad (light pass positions it by NDC rect, combine covers the screen)
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
const std::vector<uint32_t> corner_indices = { 0, 2, 1, 0, 3, 2 };

glm::vec3 mul_h(const glm::vec3& v, const glm::mat4& m)
{
    const glm::vec4 r = m * glm::vec4(v, 1.0f);
    return glm::vec3(r) / r.w;
}

glm::vec3 to_vec3(const std::array<float, 3>& v)
{
    return glm::vec3(v[0], v[1], v[2]);
}

// ============================================================================
// Free-fly camera (left-handed, matching glm::lookAtLH used across the examples)
// ============================================================================

struct FlyCamera
{
    glm::vec3 position { 0.0f };
    float yaw = 0.0f;   // radians, 0 looks down +Z
    float pitch = 0.0f; // radians, positive looks up
    float speed = 600.0f;

    glm::vec3 forward() const
    {
        return glm::vec3(std::cos(pitch) * std::sin(yaw),
                         std::sin(pitch),
                         std::cos(pitch) * std::cos(yaw));
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

// Combine-pass inspection views, cycled with G. Kept in sync with the
// DEBUG_* constants in sponza_combine_frag.glsl.
constexpr std::array<const char*, 10> debug_mode_names = {
    "final", "albedo", "normal", "diffuse light (x0.25)",
    "specular light", "specular mask", "gloss", "ambient occlusion",
    "indirect (IBL x AO)", "specular tint (metalness)"
};

struct InputState
{
    bool looking = false;
    double last_x = 0.0;
    double last_y = 0.0;
    float yaw_delta = 0.0f;
    float pitch_delta = 0.0f;
    uint32_t debug_mode = 0;
};

InputState g_input;

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action != GLFW_PRESS)
        return;
    if (key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    if (key == GLFW_KEY_G)
        g_input.debug_mode = (g_input.debug_mode + 1) % debug_mode_names.size();
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

} // namespace

void run_example_sponza()
{
    // SPONZA_DEBUG_VIEW selects the initial G-buffer view without a keypress, so
    // the pipeline can be inspected from a script.
    if (const char* debug_env = std::getenv("SPONZA_DEBUG_VIEW"))
    {
        const int requested = std::atoi(debug_env);
        if (requested > 0 && requested < static_cast<int>(debug_mode_names.size()))
            g_input.debug_mode = static_cast<uint32_t>(requested);
    }

    const SponzaAsset asset = load_hzms(spz_asset_root / "sponza.bin");
    std::printf("[sponza] %zu vertices, %zu indices, %zu submeshes, %zu materials, %zu textures\n",
                asset.vertices.size(), asset.indices.size(), asset.submeshes.size(),
                asset.materials.size(), asset.texture_names.size());

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(spz_width, spz_height, "Horizon Sponza Deferred [Vulkan]", nullptr, nullptr);
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_callback);

    Corona::Horizon::HardwareBuffer scene_vb =
        Corona::Horizon::HardwareBuffer::vertex(asset.vertices, "example_sponza.scene.vb");
    Corona::Horizon::HardwareBuffer scene_ib =
        Corona::Horizon::HardwareBuffer::index(asset.indices, "example_sponza.scene.ib");
    Corona::Horizon::HardwareBuffer quad_vb =
        Corona::Horizon::HardwareBuffer::vertex(corner_vertices, "example_sponza.quad.vb");
    Corona::Horizon::HardwareBuffer quad_ib =
        Corona::Horizon::HardwareBuffer::index(corner_indices, "example_sponza.quad.ib");

    // Every material texture stays resident in the bindless table; draws only
    // switch the indices carried by the push constant.
    std::vector<Corona::Horizon::HardwareImage> textures;
    std::vector<uint32_t> texture_descriptors;
    textures.reserve(asset.texture_names.size());
    texture_descriptors.reserve(asset.texture_names.size());
    for (const std::string& name : asset.texture_names)
    {
        textures.push_back(create_ktx_texture(spz_asset_root / "textures" / name,
                                              "example_sponza." + name));
        texture_descriptors.push_back(textures.back().store_descriptor());
    }
    const uint32_t fallback_descriptor = texture_descriptors.empty() ? 0 : texture_descriptors[0];

    const auto descriptor_of = [&](int32_t texture_index) -> uint32_t {
        if (texture_index < 0 || static_cast<size_t>(texture_index) >= texture_descriptors.size())
            return fallback_descriptor;
        return texture_descriptors[static_cast<size_t>(texture_index)];
    };

    // G-buffer: albedo + world normal + device depth (R32F colour target)
    const auto gbuffer_usage =
        Corona::Horizon::ImageUsageFlags::ColorAttachment | Corona::Horizon::ImageUsageFlags::Sampled;
    Corona::Horizon::HardwareImage gbuffer_albedo(Corona::Horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, Corona::Horizon::Format::RGBA8_UNORM, gbuffer_usage, "example_sponza.gbuffer.albedo"));
    gbuffer_albedo.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    Corona::Horizon::HardwareImage gbuffer_normal(Corona::Horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, Corona::Horizon::Format::RGBA8_UNORM, gbuffer_usage, "example_sponza.gbuffer.normal"));
    gbuffer_normal.set_clear_color(0.5f, 0.5f, 0.5f, 1.0f);
    Corona::Horizon::HardwareImage gbuffer_depth_val(Corona::Horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, Corona::Horizon::Format::R32_FLOAT, gbuffer_usage, "example_sponza.gbuffer.depthval"));
    gbuffer_depth_val.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f); // far plane

    Corona::Horizon::HardwareImage gbuffer_depth(Corona::Horizon::HardwareImageDesc::depth_attachment(
        spz_width, spz_height, Corona::Horizon::Format::D32, "example_sponza.gbuffer.depth"));
    gbuffer_depth.set_clear_depth(1.0f, 0);

    // RGBA16F rather than example_deferred's RGBA8: Sponza is large enough that
    // dozens of lights overlap across most of the frame, and an 8-bit target
    // clamps the additive accumulation to 1.0 almost everywhere, which flattens
    // the result and makes the sun and ambient terms have no visible effect.
    Corona::Horizon::HardwareImage light_buffer(Corona::Horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, Corona::Horizon::Format::RGBA16_FLOAT, gbuffer_usage, "example_sponza.lightbuffer"));
    // Alpha carries accumulated specular and the blend is additive on all four
    // channels, so it must clear to 0. Clearing it to 1 (as the other targets do)
    // seeds every pixel with a full-strength highlight.
    light_buffer.set_clear_color(0.0f, 0.0f, 0.0f, 0.0f);

    Corona::Horizon::HardwareImage final_output_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
            Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
            Corona::Horizon::ImageUsageFlags::TransferDst,
        "example_sponza.output"));
    final_output_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    // Environment probe for indirect light, reusing the DDS cubemaps example_ibl
    // ships. Sponza has no probe of its own, and an outdoor courtyard probe is a
    // reasonable stand-in for an open-roofed atrium.
    const std::filesystem::path env_root =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "env";
    const CubeMapData irradiance_dds = load_dds_cube_rgba16f(env_root / "bolonga_irr.dds");
    const CubeMapData radiance_dds = load_dds_cube_rgba16f(env_root / "bolonga_lod.dds");
    Corona::Horizon::HardwareImage env_irradiance =
        create_cubemap_image(irradiance_dds, "example_sponza.env.irr");
    Corona::Horizon::HardwareImage env_radiance =
        create_cubemap_image(radiance_dds, "example_sponza.env.lod");

    // Sun shadow map: depth from the light, in an R32F colour target. Full float
    // precision avoids the RGBA8 pack/unpack that example_shadowmaps needs.
    Corona::Horizon::HardwareImage shadow_map(Corona::Horizon::HardwareImageDesc::texture_2d(
        spz_shadow_map_size, spz_shadow_map_size, Corona::Horizon::Format::R32_FLOAT,
        gbuffer_usage, "example_sponza.shadowmap"));
    shadow_map.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f); // far plane

    Corona::Horizon::HardwareImage shadow_depth(Corona::Horizon::HardwareImageDesc::depth_attachment(
        spz_shadow_map_size, spz_shadow_map_size, Corona::Horizon::Format::D32,
        "example_sponza.shadowmap.depth"));
    shadow_depth.set_clear_depth(1.0f, 0);

    // Pass 0: sun shadow map
    Corona::Horizon::RasterizerPipelineDesc shadow_desc;
    shadow_desc.blend.attachments = { Corona::Horizon::BlendStateDesc::opaque_attachment() };

    Corona::Horizon::RasterizerPipeline shadow_rasterizer(sponza_shadow_vert_glsl,
                                                          sponza_shadow_frag_glsl, shadow_desc);
    shadow_rasterizer.outDepth = shadow_map;
    shadow_rasterizer.bind_depth_target(shadow_depth);

    // Pass 1: geometry -> G-buffer (single MRT pass, three colour attachments)
    Corona::Horizon::RasterizerPipelineDesc geom_desc;
    geom_desc.blend.attachments = { Corona::Horizon::BlendStateDesc::opaque_attachment() };

    Corona::Horizon::RasterizerPipeline geom_rasterizer(sponza_geom_vert_glsl, sponza_geom_frag_glsl, geom_desc);
    geom_rasterizer.outAlbedo = gbuffer_albedo;
    geom_rasterizer.outNormal = gbuffer_normal;
    geom_rasterizer.outDepthVal = gbuffer_depth_val;
    geom_rasterizer.bind_depth_target(gbuffer_depth);

    // Pass 2: light accumulation (additive blend, no depth)
    Corona::Horizon::RasterizerPipelineDesc light_desc;
    light_desc.depth_stencil.depth_test_enabled = false;
    light_desc.depth_stencil.depth_write_enabled = false;
    Corona::Horizon::BlendAttachmentDesc additive;
    additive.blend_enabled = true;
    additive.src_color_blend_factor = Corona::Horizon::BlendFactor::One;
    additive.dst_color_blend_factor = Corona::Horizon::BlendFactor::One;
    additive.color_blend_op = Corona::Horizon::BlendOp::Add;
    additive.src_alpha_blend_factor = Corona::Horizon::BlendFactor::One;
    additive.dst_alpha_blend_factor = Corona::Horizon::BlendFactor::One;
    additive.alpha_blend_op = Corona::Horizon::BlendOp::Add;
    light_desc.blend.attachments = { additive };

    Corona::Horizon::RasterizerPipeline light_rasterizer(sponza_light_vert_glsl, sponza_light_frag_glsl, light_desc);
    light_rasterizer.outColor = light_buffer;
    light_rasterizer.vpc.gNormalIndex = gbuffer_normal.store_descriptor();
    light_rasterizer.vpc.gDepthIndex = gbuffer_depth_val.store_descriptor();
    light_rasterizer.vpc.gShadowIndex = shadow_map.store_descriptor();

    // SSAO: raw occlusion then a cross-bilateral blur, both full screen.
    Corona::Horizon::HardwareImage ssao_raw(Corona::Horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, Corona::Horizon::Format::R32_FLOAT, gbuffer_usage,
        "example_sponza.ssao.raw"));
    ssao_raw.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f);

    Corona::Horizon::HardwareImage ssao_blurred(Corona::Horizon::HardwareImageDesc::texture_2d(
        spz_width, spz_height, Corona::Horizon::Format::R32_FLOAT, gbuffer_usage,
        "example_sponza.ssao.blurred"));
    ssao_blurred.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f);

    Corona::Horizon::RasterizerPipelineDesc fullscreen_desc;
    fullscreen_desc.depth_stencil.depth_test_enabled = false;
    fullscreen_desc.depth_stencil.depth_write_enabled = false;
    fullscreen_desc.blend.attachments = { Corona::Horizon::BlendStateDesc::opaque_attachment() };

    Corona::Horizon::RasterizerPipeline ssao_rasterizer(sponza_combine_vert_glsl,
                                                        sponza_ssao_frag_glsl, fullscreen_desc);
    ssao_rasterizer.outAo = ssao_raw;
    ssao_rasterizer.fpc.gNormalIndex = gbuffer_normal.store_descriptor();
    ssao_rasterizer.fpc.gDepthIndex = gbuffer_depth_val.store_descriptor();

    Corona::Horizon::RasterizerPipeline ssao_blur_rasterizer(sponza_combine_vert_glsl,
                                                              sponza_ssao_blur_frag_glsl,
                                                              fullscreen_desc);
    ssao_blur_rasterizer.outAo = ssao_blurred;
    ssao_blur_rasterizer.fpc.gAoIndex = ssao_raw.store_descriptor();
    ssao_blur_rasterizer.fpc.gDepthIndex = gbuffer_depth_val.store_descriptor();

    // Pass 3: combine
    Corona::Horizon::RasterizerPipelineDesc combine_desc;
    combine_desc.depth_stencil.depth_test_enabled = false;
    combine_desc.depth_stencil.depth_write_enabled = false;
    combine_desc.blend.attachments = { Corona::Horizon::BlendStateDesc::opaque_attachment() };

    Corona::Horizon::RasterizerPipeline combine_rasterizer(sponza_combine_vert_glsl, sponza_combine_frag_glsl, combine_desc);
    combine_rasterizer.outColor = final_output_image;
    combine_rasterizer.fpc.gAlbedoIndex = gbuffer_albedo.store_descriptor();
    combine_rasterizer.fpc.gLightIndex = light_buffer.store_descriptor();
    combine_rasterizer.fpc.gNormalIndex = gbuffer_normal.store_descriptor();
    combine_rasterizer.fpc.gAoIndex = ssao_blurred.store_descriptor();
    combine_rasterizer.fpc.gDepthIndex = gbuffer_depth_val.store_descriptor();
    combine_rasterizer.fpc.texCubeIrrIndex = env_irradiance.store_descriptor();
    combine_rasterizer.fpc.texCubeLodIndex = env_radiance.store_descriptor();
    // fpc.ambient (rgb: ambient floor, w: exposure) is refreshed every frame from
    // the ImGui panel; see the combine block in the render loop.

    Corona::Horizon::HardwareExecutor render_executor;
    Corona::Horizon::HardwareExecutor display_executor;
    Corona::Horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    Corona::Horizon::DrawIndexedParams quad_params;
    quad_params.index_type = Corona::Horizon::IndexType::UInt32;
    quad_params.index_count = static_cast<uint32_t>(corner_indices.size());

    // Camera starts at the viewpoint authored in the glTF scene.
    const glm::vec3 scene_min = to_vec3(asset.scene.min);
    const glm::vec3 scene_max = to_vec3(asset.scene.max);
    const glm::vec3 scene_center = (scene_min + scene_max) * 0.5f;
    const glm::vec3 scene_extent = scene_max - scene_min;

    FlyCamera camera;
    camera.position = to_vec3(asset.scene.camera_position);
    {
        const glm::vec3 dir = glm::normalize(to_vec3(asset.scene.camera_target) - camera.position);
        camera.pitch = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
        camera.yaw = std::atan2(dir.x, dir.z);
    }
    camera.speed = glm::length(scene_extent) * 0.12f;

    constexpr float aspect = static_cast<float>(spz_width) / static_cast<float>(spz_height);
    const float z_far = glm::length(scene_extent) * 2.0f;
    const glm::mat4 proj = [&] {
        glm::mat4 m = glm::perspectiveLH(asset.scene.camera_yfov, aspect, asset.scene.camera_znear, z_far);
        m[1][1] *= -1.0f; // Vulkan clip space Y flip
        return m;
    }();

    // Point lights drift through the arcade at roughly waist-to-gallery height.
    const glm::vec3 light_area(scene_extent.x * 0.40f, scene_extent.y * 0.22f, scene_extent.z * 0.16f);
    const glm::vec3 light_origin(scene_center.x, scene_min.y + scene_extent.y * 0.22f, scene_center.z);

    // Sun shadow matrix. Both the sun and the geometry are static, so this is
    // built once; the shadow pass itself is still re-recorded per frame because
    // it is cheap next to the G-buffer pass.
    const glm::vec3 sun_direction = glm::normalize(to_vec3(asset.scene.sun_direction));
    const float scene_radius = glm::length(scene_extent) * 0.5f;
    const glm::mat4 sun_view_proj = [&] {
        // Guard the up vector in case the sun ever points straight down.
        const glm::vec3 up = std::abs(sun_direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                               : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 eye = scene_center - sun_direction * scene_radius * 1.5f;
        const glm::mat4 light_view = glm::lookAtLH(eye, scene_center, up);
        // GLM_FORCE_DEPTH_ZERO_TO_ONE is set in common.h, so this is already a
        // Vulkan-style [0,1] depth range.
        glm::mat4 light_proj = glm::orthoLH(-scene_radius, scene_radius,
                                            -scene_radius, scene_radius,
                                            0.0f, scene_radius * 3.0f);
        light_proj[1][1] *= -1.0f; // Vulkan clip space Y flip
        return light_proj * light_view;
    }();
    // One shadow texel covers this much world space; used for the normal offset.
    const float shadow_texel_world = (2.0f * scene_radius) / static_cast<float>(spz_shadow_map_size);

    HorizonImGuiLayer ui(window, spz_width, spz_height);

    Tuning tuning;

    const auto start_time = std::chrono::high_resolution_clock::now();
    auto prev_time = start_time;
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;
    int visible_lights = 0;
    float light_time_accum = 0.0f;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ui.new_frame();
        ImGui::SetNextWindowSize(ImVec2(440.0f, 700.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Sponza");
        ImGui::Text("RMB drag: look   WASD: move   Q/E: down/up   Shift: fast");
        ImGui::Text("submeshes %zu   draw lights %d/%d",
                    asset.submeshes.size(), visible_lights, tuning.point_light_count + 1);
        ImGui::Separator();

        int debug_mode = static_cast<int>(g_input.debug_mode);
        if (ImGui::Combo("G-buffer view", &debug_mode, debug_mode_names.data(),
                         static_cast<int>(debug_mode_names.size())))
            g_input.debug_mode = static_cast<uint32_t>(debug_mode);

        if (ImGui::CollapsingHeader("Direct light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SeparatorText("Sun");
            ImGui::SliderFloat("sun intensity", &tuning.sun_intensity, 0.0f, 2.0f);
            ImGui::ColorEdit3("sun colour", &tuning.sun_color.x);
            ImGui::Checkbox("cast shadows", &tuning.sun_shadow);
            if (tuning.sun_shadow)
                ImGui::SliderFloat("shadow bias", &tuning.shadow_bias, 0.0f, 0.01f, "%.4f");

            ImGui::SeparatorText("Point lights");
            ImGui::SliderInt("count", &tuning.point_light_count, 0, spz_max_lights);
            ImGui::SliderFloat("intensity", &tuning.point_intensity, 0.0f, 1.0f);
            ImGui::SliderFloat("radius", &tuning.point_radius, 50.0f, 1500.0f);
            // 0 falls off from the centre; 0.8 is what example_deferred uses and
            // at this scale it saturates the accumulation buffer.
            ImGui::SliderFloat("falloff start", &tuning.point_inner, 0.0f, 0.95f);
            ImGui::Checkbox("animate", &tuning.animate_lights);
            ImGui::SameLine();
            ImGui::SliderFloat("speed", &tuning.light_speed, 0.0f, 1.5f);

            ImGui::SeparatorText("Shared");
            ImGui::SliderFloat("specular strength", &tuning.specular_strength, 0.0f, 2.0f);
        }

        if (ImGui::CollapsingHeader("Surface response", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextWrapped("All 25 glTF materials share metallic 0.588 / roughness 0.9, "
                               "so the '*_spec' maps are the only per-material signal. They "
                               "drive both intensity and gloss through these ranges.");
            ImGui::SliderFloat("specular min", &tuning.spec_min, 0.0f, 1.0f);
            ImGui::SliderFloat("specular max", &tuning.spec_max, 0.0f, 1.0f);
            ImGui::SliderFloat("gloss min", &tuning.gloss_min, 0.0f, 1.0f);
            ImGui::SliderFloat("gloss max", &tuning.gloss_max, 0.0f, 1.0f);
            ImGui::SeparatorText("No spec map (curtains, roof, ...)");
            ImGui::SliderFloat("specular##nomap", &tuning.no_map_specular, 0.0f, 1.0f);
            ImGui::SliderFloat("gloss##nomap", &tuning.no_map_gloss, 0.0f, 1.0f);

            ImGui::SeparatorText("Metalness proxy (experimental, off)");
            ImGui::TextWrapped("Does not work here: '*_spec' encodes shininess, not "
                               "metalness, and the shiniest surface is the marble floor "
                               "(197/255) not the flagpole (76). Raising this tints the "
                               "floor first. Check the 'specular tint' view to see it.");
            ImGui::SliderFloat("metal tint (0 = off)", &tuning.metal_tint, 0.0f, 1.0f);
            ImGui::SliderFloat("metal low", &tuning.metal_low, 0.0f, 1.0f);
            ImGui::SliderFloat("metal high", &tuning.metal_high, 0.0f, 1.0f);
        }

        if (ImGui::CollapsingHeader("Indirect light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SeparatorText("IBL probe");
            ImGui::SliderFloat("irradiance (diffuse)", &tuning.irradiance_scale, 0.0f, 2.0f);
            ImGui::SliderFloat("radiance (specular)", &tuning.radiance_scale, 0.0f, 2.0f);
            ImGui::ColorEdit3("ambient floor", &tuning.ambient_floor.x);

            // The SSAO passes always run; strength 0 just makes the buffer
            // uniform white, which is the visual equivalent of switching it off.
            ImGui::SeparatorText("Ambient occlusion");
            ImGui::SliderFloat("AO strength (0 = off)", &tuning.ssao_intensity, 0.0f, 2.0f);
            ImGui::SliderFloat("AO radius", &tuning.ssao_radius, 5.0f, 250.0f);
            ImGui::SliderFloat("AO bias", &tuning.ssao_bias, 0.0f, 10.0f);
        }

        if (ImGui::CollapsingHeader("Output"))
            ImGui::SliderFloat("exposure", &tuning.exposure, 0.1f, 4.0f);

        if (ImGui::Button("Reset to defaults"))
            tuning = Tuning {};
        ImGui::End();

        const auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - prev_time).count();
        prev_time = now;

        // Animation runs on its own accumulator so pausing the lights freezes
        // them in place instead of snapping when animation resumes.
        if (tuning.animate_lights)
            light_time_accum += dt * tuning.light_speed;
        const float time = light_time_accum;

        update_camera(window, camera, std::min(dt, 0.1f));

        const glm::mat4 view = camera.view();
        const glm::mat4 view_proj = proj * view;
        const glm::mat4 inv_view_proj = glm::inverse(view_proj);

        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[192];
            std::snprintf(title, sizeof(title),
                          "Horizon Sponza Deferred [Vulkan] - %zu submeshes - %d/%d lights - %.1f FPS (%.2f ms)",
                          asset.submeshes.size(), visible_lights,
                          tuning.point_light_count + 1, fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        // Pass 0: sun shadow map. Alpha-masked materials need their mask here too,
        // otherwise foliage casts the shadow of its quads instead of its silhouette.
        shadow_rasterizer.clear_records();
        shadow_rasterizer.vsp.light_view_proj = sun_view_proj;
        for (const SponzaSubmesh& submesh : asset.submeshes)
        {
            if (submesh.material < 0 || static_cast<size_t>(submesh.material) >= asset.materials.size())
                continue;
            const SponzaMaterial& material = asset.materials[static_cast<size_t>(submesh.material)];

            uint32_t flags = 0;
            if ((material.flags & hzms_material_alpha_mask) != 0 && material.mask_texture >= 0)
                flags |= shader_material_alpha_mask;

            shadow_rasterizer.model_pc.tex_mask_index = descriptor_of(material.mask_texture);
            shadow_rasterizer.model_pc.material_flags = flags;

            Corona::Horizon::DrawIndexedParams params;
            params.index_type = Corona::Horizon::IndexType::UInt32;
            params.index_count = submesh.index_count;
            params.first_index = submesh.first_index;
            shadow_rasterizer.record(scene_ib, scene_vb, params);
        }

        // Pass 1: one draw per submesh, material switched through the push constant.
        geom_rasterizer.clear_records();
        geom_rasterizer.vsp.view_proj = view_proj;
        geom_rasterizer.vsp.spec_range = glm::vec4(tuning.spec_min, tuning.spec_max,
                                                    tuning.gloss_min, tuning.gloss_max);
        geom_rasterizer.vsp.no_map_material = glm::vec4(tuning.no_map_specular,
                                                         tuning.no_map_gloss, 0.0f, 0.0f);
        for (const SponzaSubmesh& submesh : asset.submeshes)
        {
            if (submesh.material < 0 || static_cast<size_t>(submesh.material) >= asset.materials.size())
                continue;
            const SponzaMaterial& material = asset.materials[static_cast<size_t>(submesh.material)];

            uint32_t flags = 0;
            if (material.bump_texture >= 0)
                flags |= shader_material_has_normal;
            if ((material.flags & hzms_material_alpha_mask) != 0 && material.mask_texture >= 0)
                flags |= shader_material_alpha_mask;
            if (material.specular_texture >= 0)
                flags |= shader_material_has_specular;

            geom_rasterizer.model_pc.base_color_factor = glm::vec4(
                material.base_color_factor[0], material.base_color_factor[1],
                material.base_color_factor[2], material.base_color_factor[3]);
            geom_rasterizer.model_pc.tex_base_color_index = descriptor_of(material.base_color_texture);
            geom_rasterizer.model_pc.tex_normal_index = descriptor_of(material.bump_texture);
            geom_rasterizer.model_pc.tex_specular_index = descriptor_of(material.specular_texture);
            geom_rasterizer.model_pc.tex_mask_index = descriptor_of(material.mask_texture);
            geom_rasterizer.model_pc.material_flags = flags;

            Corona::Horizon::DrawIndexedParams params;
            params.index_type = Corona::Horizon::IndexType::UInt32;
            params.index_count = submesh.index_count;
            params.first_index = submesh.first_index;
            geom_rasterizer.record(scene_ib, scene_vb, params);
        }

        // Pass 1.5: SSAO over the G-buffer, then a cross-bilateral blur.
        ssao_rasterizer.clear_records();
        ssao_rasterizer.fsp.inv_view_proj = inv_view_proj;
        ssao_rasterizer.fsp.view = view;
        ssao_rasterizer.fsp.proj = proj;
        ssao_rasterizer.fsp.params = glm::vec4(tuning.ssao_radius, tuning.ssao_bias,
                                               tuning.ssao_intensity, 0.0f);
        ssao_rasterizer.record(quad_ib, quad_vb, quad_params);

        ssao_blur_rasterizer.clear_records();
        ssao_blur_rasterizer.fsp.params = glm::vec4(1.0f / static_cast<float>(spz_width),
                                                     1.0f / static_cast<float>(spz_height),
                                                     tuning.ssao_blur_rejection, 0.0f);
        ssao_blur_rasterizer.record(quad_ib, quad_vb, quad_params);

        // Pass 2: the authored sun plus animated point lights, accumulated additively.
        light_rasterizer.clear_records();
        light_rasterizer.vsp.inv_view_proj = inv_view_proj;
        light_rasterizer.vsp.view = view;
        light_rasterizer.vsp.camera_pos = glm::vec4(camera.position, 1.0f);
        light_rasterizer.vsp.sun_view_proj = sun_view_proj;
        light_rasterizer.vsp.shadow_params = glm::vec4(
            shadow_texel_world, tuning.shadow_bias, static_cast<float>(spz_shadow_map_size),
            tuning.sun_shadow ? 1.0f : 0.0f);
        light_rasterizer.vsp.light_params = glm::vec4(tuning.specular_strength, 0.0f, 0.0f, 0.0f);
        visible_lights = 0;

        // Directional sun: negative radius selects the directional branch and the
        // rect covers the whole screen.
        light_rasterizer.vpc.light_pos_radius = glm::vec4(to_vec3(asset.scene.sun_direction), -1.0f);
        light_rasterizer.vpc.light_rgb_inner_r =
            glm::vec4(tuning.sun_color * tuning.sun_intensity, 0.0f);
        light_rasterizer.vpc.rect = glm::vec4(-1.0f, -1.0f, 1.0f, 1.0f);
        light_rasterizer.record(quad_ib, quad_vb, quad_params);
        ++visible_lights;

        for (int light = 0; light < tuning.point_light_count; ++light)
        {
            const float light_time =
                time * (std::sin(light / float(std::max(1, tuning.point_light_count)) * pi_half) * 0.5f + 0.5f);
            const glm::vec3 center(
                light_origin.x + std::sin((light_time + light * 0.47f) + pi_half * 1.37f) * light_area.x,
                light_origin.y + std::cos((light_time + light * 0.69f) + pi_half * 1.49f) * light_area.y,
                light_origin.z + std::sin((light_time + light * 0.37f) + pi_half * 1.57f) * light_area.z);

            // Project the light's AABB to NDC to get its screen rect.
            glm::vec3 mn(0.0f);
            glm::vec3 mx(0.0f);
            bool first = true;
            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 p = center + glm::vec3((corner & 1) ? tuning.point_radius : -tuning.point_radius,
                                                       (corner & 2) ? tuning.point_radius : -tuning.point_radius,
                                                       (corner & 4) ? tuning.point_radius : -tuning.point_radius);
                const glm::vec3 ndc = mul_h(p, view_proj);
                if (first)
                {
                    mn = mx = ndc;
                    first = false;
                }
                else
                {
                    mn = glm::min(mn, ndc);
                    mx = glm::max(mx, ndc);
                }
            }

            if (mx.z < 0.0f)
                continue; // entirely behind the camera

            const glm::vec2 rect_min = glm::clamp(glm::vec2(mn), glm::vec2(-1.0f), glm::vec2(1.0f));
            const glm::vec2 rect_max = glm::clamp(glm::vec2(mx), glm::vec2(-1.0f), glm::vec2(1.0f));
            if (rect_min.x >= rect_max.x || rect_min.y >= rect_max.y)
                continue;

            const uint8_t val = light & 7;
            light_rasterizer.vpc.light_pos_radius = glm::vec4(center, tuning.point_radius);
            // w is the inner radius where falloff starts, as a fraction of the
            // radius. example_deferred uses 0.8, which keeps a light at full
            // strength across 80% of its reach -- fine for radius-2 lights in a
            // 30-unit scene, but here it means every point in the atrium sums a
            // dozen unattenuated lights. Falling off from the centre keeps the
            // accumulated total in a range the tone mapper can still resolve.
            light_rasterizer.vpc.light_rgb_inner_r =
                glm::vec4(((val & 0x1) ? 1.0f : 0.25f) * tuning.point_intensity,
                          ((val & 0x2) ? 1.0f : 0.25f) * tuning.point_intensity,
                          ((val & 0x4) ? 1.0f : 0.25f) * tuning.point_intensity,
                          tuning.point_inner);
            light_rasterizer.vpc.rect = glm::vec4(rect_min, rect_max);
            light_rasterizer.record(quad_ib, quad_vb, quad_params);
            ++visible_lights;
        }

        // Pass 3: albedo x (light + ambient) + specular, tone mapped
        combine_rasterizer.clear_records();
        combine_rasterizer.fpc.debugMode = g_input.debug_mode;
        combine_rasterizer.fsp.inv_view_proj = inv_view_proj;
        combine_rasterizer.fsp.camera_pos = glm::vec4(camera.position, 1.0f);
        combine_rasterizer.fsp.env_params = glm::vec4(
            tuning.irradiance_scale, tuning.radiance_scale,
            static_cast<float>(radiance_dds.mip_count), 0.0f);
        combine_rasterizer.fsp.metal_params = glm::vec4(tuning.metal_low, tuning.metal_high,
                                                         tuning.metal_tint, 0.0f);
        combine_rasterizer.fpc.ambient = glm::vec4(tuning.ambient_floor, tuning.exposure);
        combine_rasterizer.record(quad_ib, quad_vb, quad_params);

        Corona::Horizon::SubmitReceipt render_receipt =
            render_executor << shadow_rasterizer(spz_shadow_map_size, spz_shadow_map_size)
                            << geom_rasterizer(spz_width, spz_height)
                            << ssao_rasterizer(spz_width, spz_height)
                            << ssao_blur_rasterizer(spz_width, spz_height)
                            << light_rasterizer(spz_width, spz_height)
                            << combine_rasterizer(spz_width, spz_height)
                            << Corona::Horizon::submit;

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << Corona::Horizon::present(display, final_output_image)
                                         << Corona::Horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}




