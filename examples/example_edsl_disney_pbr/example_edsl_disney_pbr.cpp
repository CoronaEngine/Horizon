// Disney Principled PBR chart — EDSL version of example_disney_pbr
// (10 parameter rows x 11 value columns, UV spheres + IBL probes).

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"
#include "common.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr uint32_t disney_width = 1280;
constexpr uint32_t disney_height = 720;
constexpr int chart_row_count = 10;
constexpr int chart_col_count = 11;
constexpr int chart_sphere_count = chart_row_count * chart_col_count;

const std::filesystem::path asset_root =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";

struct CubeMapData
{
    uint32_t size = 0;
    uint32_t mip_count = 0;
    std::vector<std::byte> payload;
};

uint32_t read_u32(const std::vector<std::byte>& bytes, size_t offset)
{
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

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
        horizon::ImageUsageFlags::Sampled | horizon::ImageUsageFlags::TransferDst, name);
    desc.mip_levels = dds.mip_count;

    horizon::HardwareImage image(desc);

    horizon::HardwareBufferDesc staging_desc;
    staging_desc.element_count = dds.payload.size();
    staging_desc.element_size = 1;
    staging_desc.usage = horizon::BufferUsageFlags::TransferSrc;
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

struct LightProbe
{
    horizon::HardwareImage lod;
    horizon::HardwareImage irr;
};

LightProbe load_light_probe(const std::string& name)
{
    LightProbe probe;
    probe.lod = create_cubemap_image(load_dds_cube_rgba16f(asset_root / "env" / (name + "_lod.dds")),
                                     "example_edsl_disney_pbr." + name + ".lod");
    probe.irr = create_cubemap_image(load_dds_cube_rgba16f(asset_root / "env" / (name + "_irr.dds")),
                                     "example_edsl_disney_pbr." + name + ".irr");
    return probe;
}

struct MeshVertex
{
    std::array<float, 3> position {};
    std::array<float, 3> normal {};
};

struct MeshData
{
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
};

MeshData make_uv_sphere(int slices, int stacks)
{
    MeshData mesh;
    mesh.vertices.reserve(static_cast<size_t>((slices + 1) * (stacks + 1)));
    mesh.indices.reserve(static_cast<size_t>(slices * stacks * 6));

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

            MeshVertex vertex;
            vertex.normal = { sin_phi * cos_theta, cos_phi, sin_phi * sin_theta };
            vertex.position = vertex.normal;
            mesh.vertices.push_back(vertex);
        }
    }

    for (int y = 0; y < stacks; ++y)
    {
        for (int x = 0; x < slices; ++x)
        {
            const uint32_t i0 = static_cast<uint32_t>(y * (slices + 1) + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + static_cast<uint32_t>(slices + 1);
            const uint32_t i3 = i2 + 1;

            mesh.indices.push_back(i0);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i3);
        }
    }

    return mesh;
}

struct DisneyMaterial
{
    glm::vec3 base_color { 0.82f, 0.67f, 0.16f };
    float metallic = 0.0f;
    float roughness = 0.5f;
    float specular = 0.5f;
    float specular_tint = 0.0f;
    float subsurface = 0.0f;
    float anisotropic = 0.0f;
    float sheen = 0.0f;
    float sheen_tint = 0.5f;
    float clearcoat = 0.0f;
    float clearcoat_gloss = 1.0f;
};

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

constexpr glm::vec3 chart_row_colors[chart_row_count] = {
    glm::vec3(1.0f, 1.0f, 1.0f),
    glm::vec3(1.0f, 0.86f, 0.12f),
    glm::vec3(0.92f, 0.12f, 0.12f),
    glm::vec3(0.92f, 0.12f, 0.12f),
    glm::vec3(0.42f, 0.32f, 0.92f),
    glm::vec3(0.92f, 0.18f, 0.78f),
    glm::vec3(0.62f, 0.18f, 0.14f),
    glm::vec3(0.62f, 0.18f, 0.14f),
    glm::vec3(0.10f, 0.34f, 0.40f),
    glm::vec3(0.10f, 0.34f, 0.40f),
};

DisneyMaterial make_default_chart_material(glm::vec3 base_color)
{
    DisneyMaterial material;
    material.base_color = base_color;
    return material;
}

DisneyMaterial make_chart_material(int row, int col)
{
    const float t = static_cast<float>(col) / static_cast<float>(chart_col_count - 1);
    DisneyMaterial material = make_default_chart_material(chart_row_colors[row]);

    switch (static_cast<ChartParam>(row))
    {
    case ChartParam::Subsurface: material.subsurface = t; break;
    case ChartParam::Metallic: material.metallic = t; break;
    case ChartParam::Specular: material.specular = t; break;
    case ChartParam::SpecularTint: material.specular_tint = t; break;
    case ChartParam::Roughness: material.roughness = t; break;
    case ChartParam::Anisotropic: material.anisotropic = t; break;
    case ChartParam::Sheen: material.sheen = t; break;
    case ChartParam::SheenTint: material.sheen_tint = t; break;
    case ChartParam::Clearcoat: material.clearcoat = t; break;
    case ChartParam::ClearcoatGloss:
        material.clearcoat = 1.0f;
        material.clearcoat_gloss = t;
        break;
    }

    return material;
}

std::array<DisneyMaterial, chart_sphere_count> build_chart_materials()
{
    std::array<DisneyMaterial, chart_sphere_count> materials {};
    for (int row = 0; row < chart_row_count; ++row)
    {
        for (int col = 0; col < chart_col_count; ++col)
            materials[static_cast<size_t>(row * chart_col_count + col)] = make_chart_material(row, col);
    }
    return materials;
}

glm::vec3 chart_sphere_position(int row, int col)
{
    constexpr float spacing = 1.12f;
    const float x = (static_cast<float>(col) - (chart_col_count - 1) * 0.5f) * spacing;
    const float y = ((chart_row_count - 1) * 0.5f - static_cast<float>(row)) * spacing;
    return glm::vec3(x, y, 0.0f);
}

struct Settings
{
    float exposure = 0.0f;
    float env_rot_curr = 0.0f;
    float env_rot_dest = 0.0f;
    bool do_diffuse = true;
    bool do_specular = true;
    bool do_diffuse_ibl = true;
    bool do_specular_ibl = true;
    int current_probe = 0;
    glm::vec3 light_dir { -0.35f, 0.55f, -0.75f };
    glm::vec3 light_col { 1.0f, 1.0f, 1.0f };
};

struct OrbitCamera
{
    float yaw_curr = glm::pi<float>();
    float yaw_dest = glm::pi<float>();
    float pitch_curr = 0.42f * glm::pi<float>();
    float pitch_dest = 0.42f * glm::pi<float>();
    float dist_curr = 13.5f;
    float dist_dest = 13.5f;
    glm::vec3 target { 0.0f };

    void orbit(float dx, float dy)
    {
        yaw_dest += dx * glm::two_pi<float>();
        pitch_dest = std::clamp(pitch_dest - dy * glm::pi<float>(), 0.15f * glm::pi<float>(),
                                0.85f * glm::pi<float>());
    }

    void dolly(float dz) { dist_dest = std::clamp(dist_dest + dist_dest * dz, 4.0f, 40.0f); }

    void update(float dt)
    {
        const float amount = std::min(dt / 0.12f, 1.0f);
        yaw_curr = glm::mix(yaw_curr, yaw_dest, amount);
        pitch_curr = glm::mix(pitch_curr, pitch_dest, amount);
        dist_curr = glm::mix(dist_curr, dist_dest, amount);
    }

    [[nodiscard]] glm::vec3 position() const
    {
        const glm::vec3 dir(std::sin(pitch_curr) * std::sin(yaw_curr), std::cos(pitch_curr),
                            std::sin(pitch_curr) * std::cos(yaw_curr));
        return target + dir * dist_curr;
    }

    [[nodiscard]] glm::mat4 env_view_mtx() const
    {
        const glm::vec3 forward = glm::normalize(target - position());
        const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
        const glm::vec3 up = glm::normalize(glm::cross(forward, right));
        return glm::mat4(glm::vec4(right, 0.0f), glm::vec4(up, 0.0f), glm::vec4(forward, 0.0f),
                         glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    }
};

struct InputContext
{
    Settings settings;
    OrbitCamera camera;
    double prev_x = 0.0;
    double prev_y = 0.0;
    bool has_prev = false;
};

void cursor_callback(GLFWwindow* window, double x, double y)
{
    auto* context = static_cast<InputContext*>(glfwGetWindowUserPointer(window));
    if (ImGui::GetIO().WantCaptureMouse)
    {
        context->prev_x = x;
        context->prev_y = y;
        context->has_prev = true;
        return;
    }
    if (!context->has_prev)
    {
        context->prev_x = x;
        context->prev_y = y;
        context->has_prev = true;
        return;
    }

    const float dx = static_cast<float>(x - context->prev_x) / static_cast<float>(disney_width);
    const float dy = static_cast<float>(y - context->prev_y) / static_cast<float>(disney_height);
    context->prev_x = x;
    context->prev_y = y;

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
        context->camera.orbit(dx, dy);
    else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
        context->camera.dolly(dx + dy);
    else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
        context->settings.env_rot_dest += dx * 2.0f;
}

void scroll_callback(GLFWwindow* window, double /*dx*/, double dy)
{
    if (ImGui::GetIO().WantCaptureMouse)
        return;
    auto* context = static_cast<InputContext*>(glfwGetWindowUserPointer(window));
    context->camera.dolly(static_cast<float>(-dy) * 0.05f);
}

const ktm::fmat4x4& to_edsl_matrix(const glm::mat4& matrix)
{
    return *reinterpret_cast<const ktm::fmat4x4*>(&matrix);
}

template <typename Type, size_t N>
    requires std::is_arithmetic_v<Type>
const ktm::vec<N, Type>& to_edsl_vector(const glm::vec<N, Type>& vec)
{
    return *reinterpret_cast<const ktm::vec<N, Type>*>(&vec);
}

} // namespace

using namespace EmbeddedShader;
using namespace ktm;

struct DisneyEdslVertexProxy
{
    Float3 pos;
    Float3 normal;
};

struct DisneyEdslVaryings
{
    Float3 v_view;
    Float3 v_normal;
    Float3 v_dir;
};

struct DisneyEdslSharedProxy
{
    Float4x4 proj_view;
    Float4x4 skyEnvMtx;
    Float4x4 envMtx;
    Float4 camPos;
    Float4 flags;
    Float4 lightDir;
    Float4 lightCol;
    Float4 exposurePad;
};

void run_example_edsl_disney_pbr()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window =
        glfwCreateWindow(disney_width, disney_height, "Horizon Disney PBR [EDSL]", nullptr, nullptr);

    InputContext input;
    const std::array<DisneyMaterial, chart_sphere_count> chart_materials = build_chart_materials();

    glfwSetWindowUserPointer(window, &input);
    glfwSetCursorPosCallback(window, cursor_callback);
    glfwSetScrollCallback(window, scroll_callback);

    LightProbe probes[2] = { load_light_probe("bolonga"), load_light_probe("kyoto") };
    MeshData sphere_mesh = make_uv_sphere(48, 24);

    horizon::HardwareBuffer sphere_vb =
        horizon::HardwareBuffer::vertex(sphere_mesh.vertices, "example_edsl_disney_pbr.sphere.vb");
    horizon::HardwareBuffer sphere_ib =
        horizon::HardwareBuffer::index(sphere_mesh.indices, "example_edsl_disney_pbr.sphere.ib");

    horizon::HardwareImage final_output_image(horizon::HardwareImageDesc::texture_2d(
        disney_width, disney_height, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsageFlags::Storage | horizon::ImageUsageFlags::ColorAttachment |
            horizon::ImageUsageFlags::Sampled | horizon::ImageUsageFlags::TransferSrc |
            horizon::ImageUsageFlags::TransferDst,
        "example_edsl_disney_pbr.output"));
    final_output_image.set_clear_color(0.5f, 0.5f, 0.5f, 1.0f);

    horizon::HardwareImage depth_image(horizon::HardwareImageDesc::depth_attachment(
        disney_width, disney_height, horizon::Format::D32, "example_edsl_disney_pbr.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    horizon::RasterizerPipelineDesc desc;
    desc.blend_enabled = false;

    DisneyEdslSharedProxy shared;

    // Push constants: model (64) + 4×mat (64) = 128 bytes.
    Float4x4 per_model;
    Float4 per_mat0;
    Float4 per_mat1;
    Float4 per_mat2;
    Float4 per_mat3;
    per_model.as_push_constant();
    per_mat0.as_push_constant();
    per_mat1.as_push_constant();
    per_mat2.as_push_constant();
    per_mat3.as_push_constant();


    const float fov_height = std::tan(glm::radians(45.0f) * 0.5f);
    bool isSkyBox = false;

    auto edsl_vertex = [&](Aggregate<DisneyEdslVertexProxy> vertex) {
        Aggregate<DisneyEdslVaryings> out;

        $IF(isSkyBox)
        {
            // skybox：把网格顶点当全屏坐标用，方向由 skyEnvMtx 还原
            position() = Float4(vertex->pos->x, vertex->pos->y, Float(1.0), Float(1.0));
            Float sky_aspect = per_mat3->z;
            Float2 tex = Float2(vertex->pos->x * (Float(fov_height) * sky_aspect),
                                vertex->pos->y * Float(-fov_height));
            out->v_dir = mul(shared.skyEnvMtx, Float4(tex, Float(1.0), Float(0.0)))->xyz();
            out->v_view = Float3(Float(0.0), Float(0.0), Float(0.0));
            out->v_normal = Float3(Float(0.0), Float(0.0), Float(1.0));
        }
        $ELSE
        {
            Float4 local_pos = Float4(vertex->pos, Float(1.0));
            Float4 world_pos = mul(per_model, local_pos);
            position() = mul(shared.proj_view, world_pos);
            out->v_view = shared.camPos->xyz() - world_pos->xyz();
            out->v_normal = mul(per_model, Float4(vertex->normal, Float(0.0)))->xyz();
            out->v_dir = Float3(Float(0.0), Float(0.0), Float(0.0));
        };
        return out;
    };

    auto toLinear = [](Float3 rgb) -> Float3 {
        return pow(abs(rgb), Float3(Float(2.2), Float(2.2), Float(2.2)));
    };

    auto toFilmic = [](Float3 rgb) -> Float3 {
        Float3 mapped = max(Float3(Float(0.0), Float(0.0), Float(0.0)),
                            rgb - Float3(Float(0.004), Float(0.004), Float(0.004)));
        return (mapped * (Float(6.2) * mapped + Float3(Float(0.5), Float(0.5), Float(0.5)))) /
               (mapped * (Float(6.2) * mapped + Float3(Float(1.7), Float(1.7), Float(1.7))) +
                Float3(Float(0.06), Float(0.06), Float(0.06)));
    };

    auto fixCubeLookup = [](Float3 v, Float lod, Float topLevelCubeSize) -> Float3 {
        Float ax = abs(v->x);
        Float ay = abs(v->y);
        Float az = abs(v->z);
        Float vmax = max(max(ax, ay), az);
        Float scale = Float(1.0) - exp2(lod) / topLevelCubeSize;
        Float3 result;
        result = v;
        $IF(ax != vmax) { result->x = v->x * scale; };
        $IF(ay != vmax) { result->y = v->y * scale; };
        $IF(az != vmax) { result->z = v->z * scale; };
        return result;
    };

    auto sqr = [](Float x) -> Float { return x * x; };

    auto schlickFresnel = [](Float u) -> Float {
        Float m = clamp(Float(1.0) - u, Float(0.0), Float(1.0));
        return m * m * m * m * m;
    };

    auto edsl_log = [](Float x) -> Float {
        return Float { Ast::AST::callFunc(
            "log", Ast::AST::createType<float>(),
            Ast::Node::accessAll({ proxy_wrap(x) }, Ast::AccessPermissions::ReadOnly)) };
    };

    auto edsl_cross = [](Float3 a, Float3 b) -> Float3 {
        return Float3 { Ast::AST::callFunc(
            "cross", Ast::AST::createType<ktm::fvec3>(),
            Ast::Node::accessAll({ proxy_wrap(a), proxy_wrap(b) }, Ast::AccessPermissions::ReadOnly)) };
    };

    auto gtr1 = [&](Float NdotH, Float a) -> Float {
        Float a2 = a * a;
        Float t = Float(1.0) + (a2 - Float(1.0)) * NdotH * NdotH;
        Float result;
        result = (a2 - Float(1.0)) / (Float(3.14159265358979323846) * edsl_log(a2) * t);
        $IF(a >= Float(1.0)) { result = Float(1.0) / Float(3.14159265358979323846); };
        return result;
    };

    auto gtr2_aniso = [&](Float NdotH, Float HdotX, Float HdotY, Float ax, Float ay) -> Float {
        return Float(1.0) /
               (Float(3.14159265358979323846) * ax * ay *
                sqr(sqr(HdotX / ax) + sqr(HdotY / ay) + NdotH * NdotH));
    };

    auto smithG_GGX = [&](Float NdotV, Float alphaG) -> Float {
        Float a = alphaG * alphaG;
        Float b = NdotV * NdotV;
        return Float(1.0) / (NdotV + sqrt(a + b - a * b));
    };

    auto smithG_GGX_aniso = [&](Float NdotV, Float VdotX, Float VdotY, Float ax, Float ay) -> Float {
        return Float(1.0) / (NdotV + sqrt(sqr(VdotX * ax) + sqr(VdotY * ay) + sqr(NdotV)));
    };

    TextureCube<fvec4> texCube = probes[0].lod;
    TextureCube<fvec4> texCubeIrr = probes[0].irr;
    Texture2D<fvec4> final_output = final_output_image;

    bool bg = false;
    // x: doDiffuse, y: doSpecular, z: doDiffuseIbl, w: doSpecularIbl
    auto edsl_fragment = [&](Aggregate<DisneyEdslVaryings> in) {
        Float4 out_color;

        $IF(isSkyBox)
        {
            Float3 sky_dir;
            sky_dir = normalize(in->v_dir);
            Float bgType = per_mat3->w;
            Float3 sky_color;
            $IF(bg)
            {
                sky_color = toLinear(Float3(texture(texCubeIrr, sky_dir)->xyz()));
            }
            $ELSE
            {
                Float lod = bgType;
                Float3 fixed_dir;
                fixed_dir = fixCubeLookup(sky_dir, lod, Float(256.0));
                sky_color = toLinear(Float3(textureLod(texCube, fixed_dir, lod)->xyz()));
            };
            out_color = Float4(toFilmic(sky_color * exp2(shared.exposurePad->x)), Float(1.0));
        }
        $ELSE
        {
            Float3 N = normalize(in->v_normal);
            Float3 V = normalize(in->v_view);
            Float3 L = normalize(Float3(shared.lightDir->xyz()));

            Float3 X;
            X = normalize(edsl_cross(N, Float3(Float(0.0), Float(1.0), Float(0.0))));
            $IF(dot(X, X) < Float(1e-4))
            {
                X = normalize(edsl_cross(N, Float3(Float(1.0), Float(0.0), Float(0.0))));
            };
            Float3 Y = normalize(edsl_cross(N, X));

            Float3 baseColor = Float3(per_mat0->xyz());
            Float metallic = per_mat0->w;
            Float roughness = clamp(per_mat1->x, Float(0.001), Float(1.0));
            Float specular = per_mat1->y;
            Float specularTint = per_mat1->z;
            Float subsurface = per_mat1->w;
            Float anisotropic = per_mat2->x;
            Float sheen = per_mat2->y;
            Float sheenTint = per_mat2->z;
            Float clearcoat = per_mat2->w;
            Float clearcoatGloss = per_mat3->x;

            Float3 Cdlin = toLinear(baseColor);
            Float Cdlum = dot(Cdlin, Float3(Float(0.3), Float(0.6), Float(0.1)));
            Float3 Ctint;
            Ctint = Float3(Float(1.0), Float(1.0), Float(1.0));
            $IF(Cdlum > Float(0.0)) { Ctint = Cdlin / Cdlum; };
            Float3 Cspec0 =
                mix(specular * Float(0.08) * mix(Float3(Float(1.0), Float(1.0), Float(1.0)), Ctint, specularTint),
                    Cdlin, metallic);
            Float3 Csheen = mix(Float3(Float(1.0), Float(1.0), Float(1.0)), Ctint, sheenTint);

            Float NdotV = clamp(dot(N, V), Float(0.0), Float(1.0));
            Float NdotVraw = dot(N, V);
            Float envFresnel = mix(Cspec0->x, Float(1.0), schlickFresnel(NdotV));


            Float3 direct;
            direct = Float3(Float(0.0), Float(0.0), Float(0.0));
            $IF(input.settings.do_diffuse || input.settings.do_specular)
            {
                Float3 brdf;
                brdf = Float3(Float(0.0), Float(0.0), Float(0.0));
                Float NdotL = dot(N, L);
                $IF(NdotL >= Float(0.0))
                {
                    $IF(NdotVraw >= Float(0.0))
                    {
                        Float3 H = normalize(L + V);
                        Float NdotH;
                        NdotH = dot(N, H);
                        Float LdotH = dot(L, H);

                        Float FL = schlickFresnel(NdotL);
                        Float FV = schlickFresnel(NdotVraw);
                        Float Fd90 = Float(0.5) + Float(2.0) * LdotH * LdotH * roughness;
                        Float Fd = mix(Float(1.0), Fd90, FL) * mix(Float(1.0), Fd90, FV);

                        Float Fss90 = LdotH * LdotH * roughness;
                        Float Fss = mix(Float(1.0), Fss90, FL) * mix(Float(1.0), Fss90, FV);
                        Float ss = Float(1.25) * (Fss * (Float(1.0) / (NdotL + NdotVraw) - Float(0.5)) + Float(0.5));

                        Float aniso_aspect = sqrt(Float(1.0) - anisotropic * Float(0.9));
                        Float ax = max(Float(0.001), sqr(roughness) / aniso_aspect);
                        Float ay = max(Float(0.001), sqr(roughness) * aniso_aspect);
                        Float Ds = gtr2_aniso(NdotH, dot(H, X), dot(H, Y), ax, ay);
                        Float FH = schlickFresnel(LdotH);
                        Float3 Fs = mix(Cspec0, Float3(Float(1.0), Float(1.0), Float(1.0)), FH);
                        Float Gs = smithG_GGX_aniso(NdotL, dot(L, X), dot(L, Y), ax, ay) *
                                   smithG_GGX_aniso(NdotVraw, dot(V, X), dot(V, Y), ax, ay);

                        Float3 Fsheen = FH * sheen * Csheen;

                        Float clearcoatAlpha =
                            Float(0.1) * (Float(1.0) - clearcoatGloss) + Float(0.001) * clearcoatGloss;
                        Float Dr = gtr1(NdotH, clearcoatAlpha);
                        Float Fr = Float(0.04) * (Float(1.0) - FH) + Float(1.0) * FH;
                        Float Gr = smithG_GGX(NdotL, Float(0.25)) * smithG_GGX(NdotVraw, Float(0.25));

                        Float3 diffusePart =
                            ((Float(1.0) / Float(3.14159265358979323846)) * mix(Fd, ss, subsurface) * Cdlin + Fsheen) *
                            (Float(1.0) - metallic);
                        Float3 specPart = Gs * Fs * Ds + Float(0.25) * clearcoat * Gr * Fr * Dr;
                        brdf = diffusePart + specPart;
                    };
                };

                Float NdotLclamped = clamp(dot(N, L), Float(0.0), Float(1.0));
                Float3 lit = brdf * NdotLclamped * Float3(shared.lightCol->xyz());
                Float enable = max(shared.flags->x + shared.flags->y, Float(0.0));
                direct = lit * enable;
            };

            Float3 indirect;
            $IF(input.settings.do_diffuse_ibl || input.settings.do_specular_ibl)
            {
                Float mip = roughness * Float(5.0);
                Float3 vr = normalize(Float(2.0) * NdotVraw * N - V);
                Float3 cubeR = fixCubeLookup(normalize(Float3(mul(shared.envMtx, Float4(vr, Float(0.0)))->xyz())), mip,
                                             Float(256.0));
                Float3 cubeN = normalize(Float3(mul(shared.envMtx, Float4(N, Float(0.0)))->xyz()));

                Float3 radiance = toLinear(Float3(textureLod(texCube, cubeR, mip)->xyz()));
                Float3 irradiance = toLinear(Float3(texture(texCubeIrr, cubeN)->xyz()));

                Float3 diffuseAlbedo = Cdlin * (Float(1.0) - metallic);
                Float3 envDiffuse = diffuseAlbedo * irradiance * shared.flags->z;
                Float3 envSpecular = Cspec0 * radiance * envFresnel * shared.flags->w;
                indirect = envDiffuse + envSpecular;
            }

            Float3 color = (direct + indirect) * exp2(shared.exposurePad->x);
            out_color = Float4(toFilmic(color), Float(1.0));
        };

        final_output << out_color;
    };

    horizon::RasterizerPipeline rasterizer(edsl_vertex, edsl_fragment, desc);
    rasterizer.bind_depth_target(depth_image);

    horizon::HardwareExecutor render_executor;
    horizon::HardwareExecutor display_executor;
    horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    horizon::DrawIndexedParams sphere_params;
    sphere_params.index_count = static_cast<uint32_t>(sphere_mesh.indices.size());

    constexpr float aspect = static_cast<float>(disney_width) / static_cast<float>(disney_height);
    constexpr float sphere_scale = 0.42f;
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        m[1][1] *= -1.0f;
        return m;
    }();

    HorizonImGuiLayer ui(window, disney_width, disney_height);

    auto prev_time = std::chrono::high_resolution_clock::now();
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ui.new_frame();

        Settings& s = input.settings;
        ImGui::Begin("Disney PBR Chart (EDSL)");
        ImGui::Text("10 rows (parameter) x 11 columns (0.0 .. 1.0)");
        ImGui::Text("Rows: subsurface, metallic, specular, specularTint, roughness,");
        ImGui::Text("      anisotropic, sheen, sheenTint, clearcoat, clearcoatGloss");
        const char* probe_names[] = { "Bolonga", "Kyoto" };
        ImGui::Combo("Light Probe", &s.current_probe, probe_names, IM_ARRAYSIZE(probe_names));
        ImGui::SliderFloat("Exposure", &s.exposure, -4.0f, 4.0f);
        ImGui::Checkbox("Direct Diffuse", &s.do_diffuse);
        ImGui::Checkbox("Direct Specular", &s.do_specular);
        ImGui::Checkbox("IBL Diffuse", &s.do_diffuse_ibl);
        ImGui::Checkbox("IBL Specular", &s.do_specular_ibl);
        ImGui::ColorEdit3("Light Color", &s.light_col.x);
        ImGui::SliderFloat3("Light Direction", &s.light_dir.x, -1.0f, 1.0f);
        ImGui::Text("Camera: LMB orbit, RMB dolly, MMB rotate env, scroll zoom");
        ImGui::End();

        const auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - prev_time).count();
        prev_time = now;

        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[96];
            std::snprintf(title, sizeof(title), "Horizon Disney PBR [EDSL] - %.1f FPS (%.2f ms)", fps,
                          1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        OrbitCamera& camera = input.camera;
        camera.update(dt);
        s.env_rot_curr = glm::mix(s.env_rot_curr, s.env_rot_dest, std::min(dt / 0.12f, 1.0f));

        const glm::vec3 cam_pos = camera.position();
        const glm::mat4 view = glm::lookAtLH(cam_pos, camera.target, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 env_rot = glm::rotate(glm::mat4(1.0f), s.env_rot_curr, glm::vec3(0.0f, 1.0f, 0.0f));
        LightProbe& probe = probes[s.current_probe];

        texCube = probe.lod;
        texCubeIrr = probe.irr;

        shared.proj_view = to_edsl_matrix(proj * view);
        shared.camPos = fvec4(to_edsl_vector(cam_pos), 1.0f);
        shared.flags = fvec4(s.do_diffuse ? 1.0f : 0.0f, s.do_specular ? 1.0f : 0.0f,
                             s.do_diffuse_ibl ? 1.0f : 0.0f, s.do_specular_ibl ? 1.0f : 0.0f);
        shared.lightDir = fvec4(to_edsl_vector(glm::normalize(s.light_dir)), 0.0f);
        shared.lightCol = fvec4(to_edsl_vector(s.light_col), 0.0f);
        shared.envMtx = to_edsl_matrix(env_rot);
        shared.skyEnvMtx = to_edsl_matrix(env_rot * camera.env_view_mtx());
        shared.exposurePad = fvec4(s.exposure, 0.0f, 0.0f, 0.0f);

        rasterizer.clear_records();

        for (int row = 0; row < chart_row_count; ++row)
        {
            for (int col = 0; col < chart_col_count; ++col)
            {
                const size_t index = static_cast<size_t>(row * chart_col_count + col);
                const DisneyMaterial& material = chart_materials[index];
                const glm::vec3 position = chart_sphere_position(row, col);
                const glm::mat4 model =
                    glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), glm::vec3(sphere_scale));

                per_model = to_edsl_matrix(model);
                per_mat0 = fvec4(to_edsl_vector(material.base_color), material.metallic);
                per_mat1 = fvec4(material.roughness, material.specular, material.specular_tint, material.subsurface);
                per_mat2 = fvec4(material.anisotropic, material.sheen, material.sheen_tint, material.clearcoat);
                per_mat3 = fvec4(material.clearcoat_gloss, 0.0f, aspect, 0.0f);
                rasterizer.record(sphere_ib, sphere_vb, sphere_params);
            }
        }

        horizon::SubmitReceipt render_receipt =
            render_executor << rasterizer.extent(disney_width, disney_height) << horizon::commit();

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << horizon::present(display, final_output_image)
                                         << horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
