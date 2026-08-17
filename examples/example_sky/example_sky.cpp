// Port of bgfx examples/36-sky (Perez dynamic sky + lightmapped landscape).
// GLSL path via Helicon offline compile (include GLSL macro).

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "example_sky.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/sky_vert.glsl)
#include GLSL(shaders/sky_frag.glsl)
#include GLSL(shaders/sky_landscape_vert.glsl)
#include GLSL(shaders/sky_landscape_frag.glsl)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
constexpr uint32_t sky_width = 1280;
constexpr uint32_t sky_height = 720;
constexpr int sky_grid = 32;

const std::filesystem::path sky_asset_root =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";

using Color = glm::vec3;

// HDTV rec. 709 matrix (same layout as bgfx 36-sky).
constexpr float M_XYZ2RGB[] = {
    3.240479f, -0.969256f, 0.055648f, -1.53715f, 1.875991f, -0.204043f, -0.49853f, 0.041556f, 1.057311f,
};

Color xyzToRgb(const Color& xyz)
{
    return Color(M_XYZ2RGB[0] * xyz.x + M_XYZ2RGB[3] * xyz.y + M_XYZ2RGB[6] * xyz.z,
                 M_XYZ2RGB[1] * xyz.x + M_XYZ2RGB[4] * xyz.y + M_XYZ2RGB[7] * xyz.z,
                 M_XYZ2RGB[2] * xyz.x + M_XYZ2RGB[5] * xyz.y + M_XYZ2RGB[8] * xyz.z);
}

const std::map<float, Color> sunLuminanceXYZTable = {
    {5.0f, {0.000000f, 0.000000f, 0.000000f}},
    {7.0f, {12.703322f, 12.989393f, 9.100411f}},
    {8.0f, {13.202644f, 13.597814f, 11.524929f}},
    {9.0f, {13.192974f, 13.597458f, 12.264488f}},
    {10.0f, {13.132943f, 13.535914f, 12.560032f}},
    {11.0f, {13.088722f, 13.489535f, 12.692996f}},
    {12.0f, {13.067827f, 13.467483f, 12.745179f}},
    {13.0f, {13.069653f, 13.469413f, 12.740822f}},
    {14.0f, {13.094319f, 13.495428f, 12.678066f}},
    {15.0f, {13.142133f, 13.545483f, 12.526785f}},
    {16.0f, {13.201734f, 13.606017f, 12.188001f}},
    {17.0f, {13.182774f, 13.572725f, 11.311157f}},
    {18.0f, {12.448635f, 12.672520f, 8.267771f}},
    {20.0f, {0.000000f, 0.000000f, 0.000000f}},
};

const std::map<float, Color> skyLuminanceXYZTable = {
    {0.0f, {0.308f, 0.308f, 0.411f}},
    {1.0f, {0.308f, 0.308f, 0.410f}},
    {2.0f, {0.301f, 0.301f, 0.402f}},
    {3.0f, {0.287f, 0.287f, 0.382f}},
    {4.0f, {0.258f, 0.258f, 0.344f}},
    {5.0f, {0.258f, 0.258f, 0.344f}},
    {7.0f, {0.962851f, 1.000000f, 1.747835f}},
    {8.0f, {0.967787f, 1.000000f, 1.776762f}},
    {9.0f, {0.970173f, 1.000000f, 1.788413f}},
    {10.0f, {0.971431f, 1.000000f, 1.794102f}},
    {11.0f, {0.972099f, 1.000000f, 1.797096f}},
    {12.0f, {0.972385f, 1.000000f, 1.798389f}},
    {13.0f, {0.972361f, 1.000000f, 1.798278f}},
    {14.0f, {0.972020f, 1.000000f, 1.796740f}},
    {15.0f, {0.971275f, 1.000000f, 1.793407f}},
    {16.0f, {0.969885f, 1.000000f, 1.787078f}},
    {17.0f, {0.967216f, 1.000000f, 1.773758f}},
    {18.0f, {0.961668f, 1.000000f, 1.739891f}},
    {20.0f, {0.264f, 0.264f, 0.352f}},
    {21.0f, {0.264f, 0.264f, 0.352f}},
    {22.0f, {0.290f, 0.290f, 0.386f}},
    {23.0f, {0.303f, 0.303f, 0.404f}},
};

const Color ABCDE[] = {
    {-0.2592f, -0.2608f, -1.4630f},
    {0.0008f, 0.0092f, 0.4275f},
    {0.2125f, 0.2102f, 5.3251f},
    {-0.8989f, -1.6537f, -2.5771f},
    {0.0452f, 0.0529f, 0.3703f},
};

const Color ABCDE_t[] = {
    {-0.0193f, -0.0167f, 0.1787f},
    {-0.0665f, -0.0950f, -0.3554f},
    {-0.0004f, -0.0079f, -0.0227f},
    {-0.0641f, -0.0441f, 0.1206f},
    {-0.0033f, -0.0109f, -0.0670f},
};

class DynamicValueController
{
public:
    void SetMap(const std::map<float, Color>& keymap) { key_map_ = keymap; }

    Color GetValue(float time) const
    {
        auto it_upper = key_map_.upper_bound(time + 1e-6f);
        auto it_lower = it_upper;
        if (it_upper != key_map_.begin())
            --it_lower;

        if (it_lower == key_map_.end())
            return it_upper->second;
        if (it_upper == key_map_.end())
            return it_lower->second;
        if (it_lower->first == it_upper->first)
            return it_lower->second;

        const float t = (time - it_lower->first) / (it_upper->first - it_lower->first);
        return glm::mix(it_lower->second, it_upper->second, t);
    }

private:
    std::map<float, Color> key_map_;
};

class SunController
{
public:
    enum Month : int
    {
        January = 0,
        February,
        March,
        April,
        May,
        June,
        July,
        August,
        September,
        October,
        November,
        December
    };

    glm::vec3 north_dir {1.0f, 0.0f, 0.0f};
    glm::vec3 sun_dir {0.0f, -1.0f, 0.0f};
    glm::vec3 up_dir {0.0f, 1.0f, 0.0f};
    float latitude = 50.0f;
    Month month = June;

    void Update(float time_hours)
    {
        CalculateSunOrbit();
        UpdateSunPosition(time_hours - 12.0f);
    }

private:
    float ecliptic_obliquity_ = glm::radians(23.4f);
    float delta_ = 0.0f;

    void CalculateSunOrbit()
    {
        const float day = 30.0f * float(month) + 15.0f;
        float lambda = glm::radians(280.46f + 0.9856474f * day);
        delta_ = std::asin(std::sin(ecliptic_obliquity_) * std::sin(lambda));
    }

    void UpdateSunPosition(float hour)
    {
        const float latitude_rad = glm::radians(latitude);
        const float hh = hour * glm::pi<float>() / 12.0f;
        const float azimuth =
            std::atan2(std::sin(hh), std::cos(hh) * std::sin(latitude_rad) - std::tan(delta_) * std::cos(latitude_rad));
        const float altitude =
            std::asin(std::sin(latitude_rad) * std::sin(delta_) +
                      std::cos(latitude_rad) * std::cos(delta_) * std::cos(hh));

        const glm::mat4 rot0 = glm::rotate(glm::mat4(1.0f), -azimuth, up_dir);
        const glm::vec3 dir = glm::vec3(rot0 * glm::vec4(north_dir, 0.0f));
        const glm::vec3 uxd = glm::cross(up_dir, dir);
        const glm::mat4 rot1 = glm::rotate(glm::mat4(1.0f), altitude, uxd);
        sun_dir = glm::normalize(glm::vec3(rot1 * glm::vec4(dir, 0.0f)));
    }
};

void computePerezCoeff(float turbidity, std::array<glm::vec4, 5>& out)
{
    const Color t {turbidity, turbidity, turbidity};
    for (uint32_t i = 0; i < 5; ++i)
        out[i] = glm::vec4(ABCDE_t[i] * t + ABCDE[i], 0.0f);
}

struct ScreenPosVertex
{
    std::array<float, 2> position {};
};

struct LandscapeVertex
{
    std::array<float, 3> position {};
    std::array<float, 3> normal {};
    std::array<float, 2> texcoord {};
};

struct LandscapeMesh
{
    std::vector<LandscapeVertex> vertices;
    std::vector<uint32_t> indices;
};

uint32_t fourcc(char a, char b, char c, uint8_t d)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(d) << 24);
}

template <typename T>
T read_pod(const std::vector<std::byte>& bytes, size_t& cursor)
{
    if (cursor + sizeof(T) > bytes.size())
        throw std::runtime_error("Unexpected end of binary mesh/texture data.");
    T value {};
    std::memcpy(&value, bytes.data() + cursor, sizeof(T));
    cursor += sizeof(T);
    return value;
}

std::vector<std::byte> read_file_bytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Failed to open file: " + path.string());
    const std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<size_t>(file_size));
    file.read(reinterpret_cast<char*>(bytes.data()), file_size);
    return bytes;
}

LandscapeMesh load_landscape_mesh(const std::filesystem::path& path)
{
    const std::vector<std::byte> bytes = read_file_bytes(path);

    const uint32_t chunk_vb = fourcc('V', 'B', ' ', 0x1);
    const uint32_t chunk_ib = fourcc('I', 'B', ' ', 0x0);
    const uint32_t chunk_pri = fourcc('P', 'R', 'I', 0x0);

    constexpr uint16_t attrib_id_position = 0x0001;
    constexpr uint16_t attrib_id_normal = 0x0002;
    constexpr uint16_t attrib_id_texcoord0 = 0x0005;
    constexpr uint16_t attrib_type_id_uint8 = 0x0001;
    constexpr uint16_t attrib_type_id_float = 0x0004;

    LandscapeMesh mesh;
    uint32_t group_base_vertex = 0;
    size_t cursor = 0;

    while (cursor + sizeof(uint32_t) <= bytes.size())
    {
        const uint32_t chunk = read_pod<uint32_t>(bytes, cursor);
        if (chunk == chunk_vb)
        {
            cursor += 16 + 24 + 64; // Sphere + Aabb + Obb

            const uint8_t num_attrs = read_pod<uint8_t>(bytes, cursor);
            const uint16_t stride = read_pod<uint16_t>(bytes, cursor);

            int32_t position_offset = -1;
            int32_t normal_offset = -1;
            int32_t texcoord_offset = -1;
            uint16_t normal_type = 0;
            for (uint8_t i = 0; i < num_attrs; ++i)
            {
                const uint16_t attr_offset = read_pod<uint16_t>(bytes, cursor);
                const uint16_t attr_id = read_pod<uint16_t>(bytes, cursor);
                cursor += 1; // num
                const uint16_t type_id = read_pod<uint16_t>(bytes, cursor);
                cursor += 2; // normalized + asInt

                if (attr_id == attrib_id_position)
                    position_offset = attr_offset;
                if (attr_id == attrib_id_normal)
                {
                    normal_offset = attr_offset;
                    normal_type = type_id;
                }
                if (attr_id == attrib_id_texcoord0)
                    texcoord_offset = attr_offset;
            }

            if (position_offset < 0 || normal_offset < 0)
                throw std::runtime_error("Landscape mesh misses position/normal: " + path.string());

            const uint16_t num_vertices = read_pod<uint16_t>(bytes, cursor);
            group_base_vertex = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.reserve(mesh.vertices.size() + num_vertices);

            for (uint16_t v = 0; v < num_vertices; ++v)
            {
                const std::byte* vertex_data = bytes.data() + cursor + static_cast<size_t>(v) * stride;
                LandscapeVertex vertex;
                std::memcpy(vertex.position.data(), vertex_data + position_offset, sizeof(float) * 3);

                if (normal_type == attrib_type_id_uint8)
                {
                    uint8_t packed[3];
                    std::memcpy(packed, vertex_data + normal_offset, 3);
                    for (int c = 0; c < 3; ++c)
                        vertex.normal[c] = static_cast<float>(packed[c]) / 255.0f * 2.0f - 1.0f;
                }
                else if (normal_type == attrib_type_id_float)
                {
                    std::memcpy(vertex.normal.data(), vertex_data + normal_offset, sizeof(float) * 3);
                }
                else
                {
                    throw std::runtime_error("Unsupported normal attribute type in landscape mesh.");
                }

                if (texcoord_offset >= 0)
                    std::memcpy(vertex.texcoord.data(), vertex_data + texcoord_offset, sizeof(float) * 2);

                mesh.vertices.push_back(vertex);
            }
            cursor += static_cast<size_t>(num_vertices) * stride;
        }
        else if (chunk == chunk_ib)
        {
            const uint32_t num_indices = read_pod<uint32_t>(bytes, cursor);
            mesh.indices.reserve(mesh.indices.size() + num_indices);
            for (uint32_t i = 0; i < num_indices; ++i)
            {
                const uint16_t index = read_pod<uint16_t>(bytes, cursor);
                mesh.indices.push_back(group_base_vertex + index);
            }
        }
        else if (chunk == chunk_pri)
        {
            const uint16_t material_len = read_pod<uint16_t>(bytes, cursor);
            cursor += material_len;
            const uint16_t num_prims = read_pod<uint16_t>(bytes, cursor);
            for (uint16_t i = 0; i < num_prims; ++i)
            {
                const uint16_t name_len = read_pod<uint16_t>(bytes, cursor);
                cursor += name_len;
                cursor += 16 + 16 + 24 + 64; // 4×u32 + Sphere + Aabb + Obb
            }
        }
        else
        {
            throw std::runtime_error("Unsupported chunk in landscape mesh: " + path.string());
        }
    }

    if (mesh.vertices.empty() || mesh.indices.empty())
        throw std::runtime_error("Failed to parse landscape mesh: " + path.string());
    return mesh;
}

horizon::HardwareImage upload_rgba8_texture(uint32_t width,
                                                    uint32_t height,
                                                    std::span<const std::byte> rgba,
                                                    const std::string& debug_name)
{
    // GPU textures default to CpuAccessMode::None, so HardwareImage::write() is rejected
    // by validation (hardware_validation.cpp). Upload via host-visible staging + GPU copy,
    // matching examples/common.cpp::create_uploaded_image.
    horizon::HardwareImageDesc desc = horizon::HardwareImageDesc::texture_2d(
        width, height, horizon::Format::RGBA8_UNORM,
        horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferDst,
        debug_name);

    horizon::HardwareImage image(desc);
    if (!rgba.empty())
    {
        horizon::HardwareBufferDesc staging_desc;
        staging_desc.element_count = rgba.size_bytes();
        staging_desc.element_size = 1;
        staging_desc.usage = horizon::BufferUsage_TransferSrc;
        staging_desc.cpu_access = horizon::CpuAccessMode::Write;

        horizon::HardwareBuffer staging(staging_desc, rgba);
        horizon::HardwareExecutor executor;
        (void)(executor.stream() << image.copy_from(staging) << horizon::commit());
    }
    return image;
}

horizon::HardwareImage load_ktx_rgba8_or_white(const std::filesystem::path& path)
{
    try
    {
        const std::vector<std::byte> bytes = read_file_bytes(path);
        static constexpr uint8_t ktx_id[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
        if (bytes.size() < 64 || std::memcmp(bytes.data(), ktx_id, 12) != 0)
            throw std::runtime_error("Not a KTX1 file");

        size_t cursor = 12;
        const uint32_t endianness = read_pod<uint32_t>(bytes, cursor);
        if (endianness != 0x04030201)
            throw std::runtime_error("Unsupported KTX endianness");

        cursor += sizeof(uint32_t) * 5; // glType..glBaseInternalFormat
        const uint32_t width = read_pod<uint32_t>(bytes, cursor);
        const uint32_t height = read_pod<uint32_t>(bytes, cursor);
        cursor += sizeof(uint32_t); // pixelDepth
        cursor += sizeof(uint32_t); // numberOfArrayElements
        const uint32_t faces = read_pod<uint32_t>(bytes, cursor);
        const uint32_t mips = std::max(1u, read_pod<uint32_t>(bytes, cursor));
        const uint32_t kv_bytes = read_pod<uint32_t>(bytes, cursor);
        cursor += kv_bytes;
        if (faces != 1 || width == 0 || height == 0)
            throw std::runtime_error("Unexpected KTX dimensions");

        // Use mip0 only.
        const uint32_t image_size = read_pod<uint32_t>(bytes, cursor);
        if (cursor + image_size > bytes.size())
            throw std::runtime_error("KTX image truncated");

        // Assume tightly packed RGBA8 (bgfx lightmap.ktx).
        const uint64_t expected = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull;
        if (image_size < expected)
            throw std::runtime_error("KTX mip0 smaller than RGBA8 expectation");

        (void)mips;
        return upload_rgba8_texture(
            width, height,
            std::span<const std::byte>(bytes.data() + cursor, static_cast<size_t>(expected)),
            "example_sky.lightmap");
    }
    catch (...)
    {
        // Fallback: white 1x1 so landscape still lights without AO.
        const uint8_t white[4] = {255, 255, 255, 255};
        return upload_rgba8_texture(
            1, 1, std::as_bytes(std::span<const uint8_t>(white, 4)), "example_sky.lightmap.white");
    }
}

void build_sky_grid(int vertical_count, int horizontal_count,
                    std::vector<ScreenPosVertex>& vertices, std::vector<uint32_t>& indices)
{
    vertices.clear();
    indices.clear();
    vertices.resize(static_cast<size_t>(vertical_count) * static_cast<size_t>(horizontal_count));

    for (int i = 0; i < vertical_count; ++i)
    {
        for (int j = 0; j < horizontal_count; ++j)
        {
            ScreenPosVertex& v = vertices[static_cast<size_t>(i) * horizontal_count + j];
            v.position = {float(j) / float(horizontal_count - 1) * 2.0f - 1.0f,
                          float(i) / float(vertical_count - 1) * 2.0f - 1.0f};
        }
    }

    indices.reserve(static_cast<size_t>(vertical_count - 1) * static_cast<size_t>(horizontal_count - 1) * 6);
    for (int i = 0; i < vertical_count - 1; ++i)
    {
        for (int j = 0; j < horizontal_count - 1; ++j)
        {
            const uint32_t i0 = static_cast<uint32_t>(j + 0 + horizontal_count * (i + 0));
            const uint32_t i1 = static_cast<uint32_t>(j + 1 + horizontal_count * (i + 0));
            const uint32_t i2 = static_cast<uint32_t>(j + 0 + horizontal_count * (i + 1));
            const uint32_t i3 = static_cast<uint32_t>(j + 1 + horizontal_count * (i + 1));
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i3);
            indices.push_back(i2);
        }
    }
}

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}
} // namespace

void run_example_sky()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(sky_width, sky_height, "Horizon Sky [GLSL]", nullptr, nullptr);
    glfwSetKeyCallback(window, key_callback);

    std::vector<ScreenPosVertex> sky_vertices;
    std::vector<uint32_t> sky_indices;
    build_sky_grid(sky_grid, sky_grid, sky_vertices, sky_indices);

    LandscapeMesh landscape = load_landscape_mesh(sky_asset_root / "meshes" / "test_scene.bin");
    horizon::HardwareImage lightmap = load_ktx_rgba8_or_white(sky_asset_root / "textures" / "lightmap.ktx");

    horizon::HardwareBuffer sky_vb =
        horizon::HardwareBuffer::vertex(sky_vertices, "example_sky.sky.vb");
    horizon::HardwareBuffer sky_ib =
        horizon::HardwareBuffer::index(sky_indices, "example_sky.sky.ib");
    horizon::HardwareBuffer landscape_vb =
        horizon::HardwareBuffer::vertex(landscape.vertices, "example_sky.landscape.vb");
    horizon::HardwareBuffer landscape_ib =
        horizon::HardwareBuffer::index(landscape.indices, "example_sky.landscape.ib");

    horizon::HardwareImage final_output_image(horizon::HardwareImageDesc::texture_2d(
        sky_width, sky_height, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_ColorAttachment |
            horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferSrc |
            horizon::ImageUsage_TransferDst,
        "example_sky.output"));
    final_output_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    horizon::HardwareImage depth_image(horizon::HardwareImageDesc::depth_attachment(
        sky_width, sky_height, horizon::Format::D32, "example_sky.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    // Landscape first (clears color+depth), then sky into remaining far-plane pixels.
    horizon::RasterizerPipelineDesc landscape_desc;
    landscape_desc.blend_enabled = false;
    landscape_desc.clear_color_target = true;
    landscape_desc.clear_depth_target = true;

    horizon::RasterizerPipeline landscape_pipeline(sky_landscape_vert_glsl, sky_landscape_frag_glsl,
                                                          landscape_desc);
    landscape_pipeline.outColor = final_output_image;
    landscape_pipeline.bind_depth_target(depth_image);
    landscape_pipeline.pc.lightmapIndex = lightmap.store_descriptor();

    horizon::RasterizerPipelineDesc sky_desc;
    sky_desc.blend_enabled = false;
    sky_desc.depth_test_enabled = true;
    sky_desc.depth_write_enabled = false;
    sky_desc.depth_compare_op = horizon::CompareOp::Equal;
    sky_desc.clear_color_target = false;
    sky_desc.clear_depth_target = false;

    horizon::RasterizerPipeline sky_pipeline(sky_vert_glsl, sky_frag_glsl, sky_desc);
    sky_pipeline.outColor = final_output_image;
    sky_pipeline.bind_depth_target(depth_image);

    horizon::HardwareExecutor render_executor;
    horizon::HardwareExecutor display_executor;
    horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    horizon::DrawIndexedParams sky_params;
    sky_params.index_count = static_cast<uint32_t>(sky_indices.size());

    horizon::DrawIndexedParams landscape_params;
    landscape_params.index_count = static_cast<uint32_t>(landscape.indices.size());

    DynamicValueController sun_lum_xyz;
    DynamicValueController sky_lum_xyz;
    sun_lum_xyz.SetMap(sunLuminanceXYZTable);
    sky_lum_xyz.SetMap(skyLuminanceXYZTable);

    SunController sun;
    float turbidity = 2.15f;
    float time_of_day = 10.0f;
    float time_scale = 1.0f;

    // Match bgfx default camera: pos (5,3,0), V=pi/8, H=-pi/3.
    const float horizontal = -glm::pi<float>() / 3.0f;
    const float vertical = glm::pi<float>() / 8.0f;
    const glm::vec3 eye(5.0f, 3.0f, 0.0f);
    const glm::vec3 forward(std::cos(vertical) * std::sin(horizontal), std::sin(vertical),
                            std::cos(vertical) * std::cos(horizontal));
    const glm::mat4 view = glm::lookAt(eye, eye + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspective(glm::radians(60.0f), sky_width / float(sky_height), 0.1f, 2000.0f);
        m[1][1] *= -1.0f;
        return m;
    }();
    const glm::mat4 view_proj = proj * view;
    const glm::mat4 inv_view_proj = glm::inverse(view_proj);

    HorizonImGuiLayer ui(window, sky_width, sky_height);

    auto prev_time = std::chrono::high_resolution_clock::now();
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;
    horizon::SubmitReceipt render_receipt;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ui.new_frame();

        ImGui::Begin("ProceduralSky");
        ImGui::SliderFloat("Time scale", &time_scale, 0.0f, 1.0f);
        ImGui::SliderFloat("Time", &time_of_day, 0.0f, 24.0f);
        ImGui::SliderFloat("Latitude", &sun.latitude, -90.0f, 90.0f);
        ImGui::SliderFloat("Turbidity", &turbidity, 1.9f, 10.0f);
        const char* months[] = {"January", "February", "March",     "April",   "May",      "June",
                                "July",    "August",   "September", "October", "November", "December"};
        ImGui::Combo("Month", reinterpret_cast<int*>(&sun.month), months, 12);
        ImGui::End();

        const auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - prev_time).count();
        prev_time = now;

        time_of_day = std::fmod(time_of_day + time_scale * dt, 24.0f);
        if (time_of_day < 0.0f)
            time_of_day += 24.0f;

        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[160];
            std::snprintf(title, sizeof(title), "Horizon Sky [GLSL] %.1f FPS (%.2f ms)", fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        sun.Update(time_of_day);
        const Color sun_xyz = sun_lum_xyz.GetValue(time_of_day);
        const Color sky_xyz = sky_lum_xyz.GetValue(time_of_day);
        const Color sun_rgb = xyzToRgb(sun_xyz);
        const Color sky_rgb = xyzToRgb(sky_xyz);

        std::array<glm::vec4, 5> perez {};
        computePerezCoeff(turbidity, perez);

        const glm::vec4 parameters(0.02f, 3.0f, 0.1f, time_of_day);

        sky_pipeline.clear_records();
        sky_pipeline.sky.invViewProj = inv_view_proj;
        sky_pipeline.sky.sunDirection = glm::vec4(sun.sun_dir, 0.0f);
        sky_pipeline.sky.skyLuminanceXYZ = glm::vec4(sky_xyz, 0.0f);
        sky_pipeline.sky.sunLuminance = glm::vec4(sun_rgb, 0.0f);
        sky_pipeline.sky.parameters = parameters;
        sky_pipeline.sky.perez0 = perez[0];
        sky_pipeline.sky.perez1 = perez[1];
        sky_pipeline.sky.perez2 = perez[2];
        sky_pipeline.sky.perez3 = perez[3];
        sky_pipeline.sky.perez4 = perez[4];
        // Fragment stage uses sky_fs to avoid VS/FS ResourceBindings clash.
        sky_pipeline.sky_fs.invViewProj = inv_view_proj;
        sky_pipeline.sky_fs.sunDirection = glm::vec4(sun.sun_dir, 0.0f);
        sky_pipeline.sky_fs.skyLuminanceXYZ = glm::vec4(sky_xyz, 0.0f);
        sky_pipeline.sky_fs.sunLuminance = glm::vec4(sun_rgb, 0.0f);
        sky_pipeline.sky_fs.parameters = parameters;
        sky_pipeline.sky_fs.perez0 = perez[0];
        sky_pipeline.sky_fs.perez1 = perez[1];
        sky_pipeline.sky_fs.perez2 = perez[2];
        sky_pipeline.sky_fs.perez3 = perez[3];
        sky_pipeline.sky_fs.perez4 = perez[4];
        sky_pipeline.record(sky_ib, sky_vb, sky_params);

        landscape_pipeline.clear_records();
        landscape_pipeline.ls.viewProj = view_proj;
        landscape_pipeline.ls.sunDirection = glm::vec4(sun.sun_dir, 0.0f);
        landscape_pipeline.ls.sunLuminance = glm::vec4(sun_rgb, 0.0f);
        landscape_pipeline.ls.skyLuminance = glm::vec4(sky_rgb, 0.0f);
        landscape_pipeline.ls.parameters = parameters;
        // Fragment stage uses a distinct UBO instance name to avoid C++ binding clash.
        landscape_pipeline.ls_fs.viewProj = view_proj;
        landscape_pipeline.ls_fs.sunDirection = glm::vec4(sun.sun_dir, 0.0f);
        landscape_pipeline.ls_fs.sunLuminance = glm::vec4(sun_rgb, 0.0f);
        landscape_pipeline.ls_fs.skyLuminance = glm::vec4(sky_rgb, 0.0f);
        landscape_pipeline.ls_fs.parameters = parameters;
        landscape_pipeline.pc.model = glm::mat4(1.0f);
        landscape_pipeline.pc.lightmapIndex = lightmap.store_descriptor();
        landscape_pipeline.record(landscape_ib, landscape_vb, landscape_params);

        render_receipt = render_executor << landscape_pipeline.extent(sky_width, sky_height)
                                         << sky_pipeline.extent(sky_width, sky_height)
                                         << horizon::commit();

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << horizon::present(display, final_output_image)
                                         << horizon::commit());
    }

    display_executor.wait_idle(render_receipt);
    glfwDestroyWindow(window);
    glfwTerminate();
}
