#include "dsl/dsl.h"
#include "rhi/common.h"
#include "Horizon.h"
#include "rhi/context.h"
#include "math/transform.h"

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#undef STB_IMAGE_WRITE_IMPLEMENTATION
#undef STB_IMAGE_WRITE_STATIC

using ocarina::Float;
using ocarina::Float2;
using ocarina::Float3;
using ocarina::Float4;
using ocarina::make_float2;
using ocarina::make_float3;
using ocarina::make_float4;

constexpr int chart_row_count = 10;
constexpr int chart_col_count = 11;

struct DisneyChartMaterial
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

static DisneyChartMaterial make_chart_material(int row, int col);
static glm::vec3 chart_sphere_position(int row, int col);
static void make_sphere(ocarina::vector<ocarina::float3>& positions, ocarina::vector<ocarina::uint3>& triangles);
// All six faces of each prefiltered DDS mip are stored side by side in a 2D texture.
struct DisneyEnvironment {
    ocarina::Texture2D atlas;
    unsigned face_size, levels;
};
static DisneyEnvironment load_environment(ocarina::Device& device, const char* filename);
static ocarina::Float3 sample_environment(const DisneyEnvironment& environment, ocarina::Float3 direction, ocarina::Float mip);
static ocarina::Float3 direct_brdf(ocarina::Float3 normal, ocarina::Float3 view, ocarina::Float3 light,
                            ocarina::Float3 base, ocarina::Float3 specular_color, ocarina::Float4 mat0,
                            ocarina::Float4 mat1, ocarina::Float4 mat2, ocarina::Float gloss);

constexpr uint32_t image_width = 960, image_height = 540;

static int env_int(const char* name, int fallback);

class DisneyRayDisplay {
public:
    explicit DisneyRayDisplay(GLFWwindow* window);
    ~DisneyRayDisplay() { copy_.waitForDeferredResources(); }

    void present(std::span<const uint32_t> pixels);

private:
    GLFWwindow* window_;
    int remaining_frames_ = env_int("HORIZON_PRESENT_FRAMES", 0);
    HardwareDisplayer surface_;
    HardwareExecutor copy_;
    HardwareBuffer staging_{image_width * image_height * 4, BufferUsage::StorageBuffer};
    HardwareImage image_{image_width, image_height, ImageFormat::RGBA8_SRGB, ImageUsage::SampledImage};
};

static ocarina::Device open_device();

static auto open_window(int width, int height, const char* title) {
    const bool hidden = env_int("HORIZON_BENCH_HIDDEN", 0) != 0;
    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    if (hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }
    glfwSetWindowSizeLimits(window, width, height, GLFW_DONT_CARE, GLFW_DONT_CARE);
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

void run_example_disney_ocarina() {
    constexpr auto mode = "disney_ocarina";
    auto device = open_device();
    auto stream = device.create_stream();
    auto window = open_window(image_width, image_height, mode);

    auto radiance = load_environment(device, "bolonga_lod.dds");
    auto irradiance = load_environment(device, "bolonga_irr.dds");
    ocarina::vector<ocarina::float3> positions;
    ocarina::vector<ocarina::uint3> triangles;
    make_sphere(positions, triangles);
    auto vertices = device.create_buffer<ocarina::float3>(positions.size());
    auto indices = device.create_buffer<ocarina::uint3>(triangles.size());
    stream << vertices.upload(positions.data()) << indices.upload(triangles.data()) << ocarina::synchronize() << ocarina::commit();
    auto mesh = device.create_mesh(vertices.view(), indices.view(), ocarina::FAST_TRACE, ocarina::DISABLE_ANYHIT);
    stream << mesh.build_bvh() << ocarina::synchronize() << ocarina::commit();
    auto chart = device.create_accel(ocarina::FAST_TRACE);

    // Each instance gets three chart material vectors, followed by its XY centre, radius and clearcoat gloss.
    ocarina::vector<ocarina::float4> material_data;
    for (int row = 0; row < chart_row_count; ++row) {
        for (int col = 0; col < chart_col_count; ++col) {
            const auto material = make_chart_material(row, col);
            const auto centre = chart_sphere_position(row, col);
            constexpr float radius = 0.42f;
            const auto transform = ocarina::translation<ocarina::H>(centre.x, centre.y, centre.z) * ocarina::scale<ocarina::H>(radius);
            if (row == 0 && col == 0)
                chart.add_instance(ocarina::move(mesh), transform);
            else
                chart.add_instance(0u, transform);
            material_data.push_back(make_float4(material.base_color.x, material.base_color.y,
                                                material.base_color.z, material.metallic));
            material_data.push_back(make_float4(material.roughness, material.specular,
                                                material.specular_tint, material.subsurface));
            material_data.push_back(make_float4(material.anisotropic, material.sheen,
                                                material.sheen_tint, material.clearcoat));
            material_data.push_back(make_float4(centre.x, centre.y, radius, material.clearcoat_gloss));
        }
    }
    auto materials = device.create_buffer<ocarina::float4>(material_data.size());
    stream << materials.upload(material_data.data()) << chart.build_bvh() << ocarina::synchronize() << ocarina::commit();

    ocarina::Kernel trace = [&](Float3 eye, Float3 light, Float angle, ocarina::BufferVar<unsigned> output) {
        ocarina::Uint pixel = ocarina::dispatch_id();
        Float3 forward = normalize(-eye);
        Float3 right = normalize(cross(make_float3(0.f, 1.f, 0.f), forward));
        Float3 up = cross(forward, right);
        Float screen_x = (ocarina::cast<float>(pixel % image_width) + .5f) / image_width * 2.f - 1.f;
        Float screen_y = 1.f - (ocarina::cast<float>(pixel / image_width) + .5f) / image_height * 2.f;
        constexpr float aspect = float(image_width) / image_height;
        constexpr float half_fov = .41421356f; // tan(45 degrees / 2)
        auto ray = make_ray(eye, normalize(forward + half_fov * (right * screen_x * aspect + up * screen_y)));
        auto hit = chart.trace_closest(ray);
        Float3 color = make_float3(.5f);
        $if (hit->is_hit()) {
            ocarina::Uint3 tri = indices.read(hit->prim_id);
            Float3 local = hit->triangle_lerp(vertices.read(tri.x), vertices.read(tri.y), vertices.read(tri.z));
            ocarina::Uint start = 4u * hit->inst_id;
            Float4 m0 = materials.read(start), m1 = materials.read(start + 1u), m2 = materials.read(start + 2u);
            Float4 placement = materials.read(start + 3u);
            Float3 point = make_float3(placement.x, placement.y, 0.f) + placement.z * local;
            Float3 n = normalize(local), v = -ray->direction();
            Float3 base = pow(m0.xyz(), make_float3(2.2f));
            Float3 tint = base / max(dot(base, make_float3(.3f, .6f, .1f)), .0001f);
            Float3 spec = lerp(m0.w, m1.y * .08f * lerp(m1.z, make_float3(1.f), tint), base);
            Float3 direct = make_float3(0.f);
            $if (dot(n, light) > 0.f) {
                ocarina::Bool shadow = chart.trace_occlusion(make_ray(offset_ray_origin(point, n), light));
                direct = direct_brdf(n, v, light, base, spec, m0, m1, m2, placement.w) *
                         max(dot(n, light), 0.f) * select(shadow, 0.f, 1.f);
            };
            auto rotate_env = [&](Float3 d) {
                return make_float3(cos(angle) * d.x + sin(angle) * d.z, d.y,
                                   -sin(angle) * d.x + cos(angle) * d.z);
            };
            Float3 reflected = 2.f * dot(n, v) * n - v;
            Float3 diffuse_env = sample_environment(irradiance, rotate_env(n), 0.f);
            Float3 spec_env = sample_environment(radiance, rotate_env(reflected), m1.x * 5.f);
            Float fresnel = pow(1.f - clamp(dot(n, v), 0.f, 1.f), 5.f);
            Float3 lit =
                direct + base * (1.f - m0.w) * diffuse_env + lerp(fresnel, spec, make_float3(1.f)) * spec_env;
            Float3 x = max(lit - .004f, make_float3(0.f));
            color = x * (6.2f * x + .5f) / (x * (6.2f * x + 1.7f) + .06f);
        };
        Float3 bytes = clamp(color, 0.f, 1.f) * 255.f + .5f;
        output.write(pixel, ocarina::cast<unsigned>(bytes.x) | (ocarina::cast<unsigned>(bytes.y) << 8u) |
                            (ocarina::cast<unsigned>(bytes.z) << 16u) | 0xff000000u);
    };
    auto shader = device.compile(trace, mode);
    ocarina::vector<unsigned> pixels(image_width * image_height);
    auto output = device.create_buffer<unsigned>(pixels.size());
    DisneyRayDisplay display(window.get());
    const auto direction = glm::normalize(glm::vec3(-0.35f, 0.55f, -0.75f));
    const auto light = make_float3(direction.x, direction.y, direction.z);
    const double start = glfwGetTime();
    float fixed_dt = 0.f;
    if (const char* value = std::getenv("HORIZON_FIXED_DT"); value && *value) {
        fixed_dt = std::stof(value);
        if (!std::isfinite(fixed_dt) || fixed_dt < 0)
            throw std::invalid_argument("Invalid HORIZON_FIXED_DT");
    }
    constexpr float default_yaw = glm::pi<float>(), default_pitch = 0.42f * glm::pi<float>();
    float yaw = default_yaw, pitch = default_pitch, distance = 15.f, previous = 0.f;
    for (uint64_t frame = 0; !glfwWindowShouldClose(window.get()); ++frame) {
        glfwPollEvents();
        const float now = fixed_dt ? static_cast<float>(frame) * fixed_dt : static_cast<float>(glfwGetTime() - start);
        const float blend = std::min((now - previous) / 0.12f, 1.f);
        previous = now;
        // Preserve the original camera's per-frame rounding for reproducible captures.
        yaw = glm::mix(yaw, default_yaw, blend);
        pitch = glm::mix(pitch, default_pitch, blend);
        distance = glm::mix(distance, 15.f, blend);
        const glm::vec3 direction_from_target(std::sin(pitch) * std::sin(yaw), std::cos(pitch),
                                              std::sin(pitch) * std::cos(yaw));
        const glm::vec3 position = direction_from_target * distance;
        const auto eye = make_float3(position.x, position.y, position.z);
        stream << shader(eye, light, 0.f, output).dispatch(pixels.size())
               << output.download(pixels.data()) << ocarina::synchronize() << ocarina::commit();
        display.present(pixels);
    }
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

static DisneyChartMaterial make_chart_material(int row, int col)
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

    const float t = static_cast<float>(col) / static_cast<float>(chart_col_count - 1);
    DisneyChartMaterial material{.base_color = chart_row_colors[row]};

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

static glm::vec3 chart_sphere_position(int row, int col)
{
    constexpr float spacing = 1.12f;
    const float x = (static_cast<float>(col) - (chart_col_count - 1) * 0.5f) * spacing;
    const float y = ((chart_row_count - 1) * 0.5f - static_cast<float>(row)) * spacing;
    return glm::vec3(x, y, 0.0f);
}

static void make_sphere(ocarina::vector<ocarina::float3>& positions, ocarina::vector<ocarina::uint3>& triangles) {
    constexpr int slices = 48, stacks = 24;
    positions.reserve((slices + 1) * (stacks + 1));
    triangles.reserve(2 * slices * stacks);
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

            positions.push_back(ocarina::make_float3(sin_phi * cos_theta, cos_phi, sin_phi * sin_theta));
        }
    }

    // Match the winding and primitive order of the chart's sphere mesh.
    for (int y = 0; y < stacks; ++y) {
        for (int x = 0; x < slices; ++x) {
            const unsigned top = y * (slices + 1) + x;
            const unsigned bottom = top + slices + 1;
            triangles.push_back(ocarina::make_uint3(top, bottom, top + 1));
            triangles.push_back(ocarina::make_uint3(top + 1, bottom, bottom + 1));
        }
    }
}

static uint32_t read_u32(const std::vector<std::byte>& bytes, size_t offset)
{
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

static auto read_cubemap(const char* filename)
{
    struct CubeMapData
    {
        uint32_t size = 0;
        uint32_t mip_count = 0;
        std::vector<std::byte> payload;
    };

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

    CubeMapData data;
    data.size = width;
    data.mip_count = mip_count;
    data.payload.assign(bytes.begin() + dx10_payload_offset, bytes.end());
    return data;
}
static DisneyEnvironment load_environment(ocarina::Device& device, const char* filename) {
    const auto dds = read_cubemap(filename);
    if (!std::has_single_bit(dds.size) || dds.mip_count > std::bit_width(dds.size))
        throw std::runtime_error("Expected a power-of-two environment cubemap");
    const unsigned width = 6 * dds.size, height = 2 * dds.size;
    ocarina::vector<uint64_t> texels(size_t(width) * height); // one RGBA16F texel per word
    size_t offset = 0;
    for (unsigned face = 0; face < 6; ++face) {
        for (unsigned mip = 0; mip < dds.mip_count; ++mip) {
            const unsigned side = dds.size >> mip;
            const unsigned top = 2 * (dds.size - side);
            for (unsigned y = 0; y < side; ++y) {
                const size_t bytes = size_t(side) * sizeof(uint64_t);
                if (offset + bytes > dds.payload.size())
                    throw std::runtime_error("Truncated environment cubemap");
                std::memcpy(&texels[size_t(top + y) * width + face * side], dds.payload.data() + offset, bytes);
                offset += bytes;
            }
        }
    }
    auto atlas = device.create_texture2d(ocarina::make_uint2(width, height), ocarina::PixelStorage::HALF4);
    atlas.upload_immediately(texels.data());
    return {ocarina::move(atlas), dds.size, dds.mip_count};
}

static Float3 sample_environment(const DisneyEnvironment& env, Float3 d, Float mip) {
    Float3 axis = abs(d);
    Float major = max(axis.x, max(axis.y, axis.z));
    Float face;
    Float2 uv;
    $if (axis.x == major) {
        face = select(d.x > 0.f, 0.f, 1.f);
        uv = make_float2(select(d.x > 0.f, -d.z, d.z), -d.y);
    }
    $else {
        $if (axis.y == major) {
            face = select(d.y > 0.f, 2.f, 3.f);
            uv = make_float2(d.x, select(d.y > 0.f, d.z, -d.z));
        }
        $else {
            face = select(d.z > 0.f, 4.f, 5.f);
            uv = make_float2(select(d.z > 0.f, d.x, -d.x), -d.y);
        };
    };
    uv = .5f * (uv / major + 1.f);
    auto sample_level = [&](Float level) -> Float3 {
        Float side = float(env.face_size) * exp2(-level);
        Float2 xy = clamp(uv * side, make_float2(.5f), make_float2(side - .5f));
        xy += make_float2(face * side, 2.f * (float(env.face_size) - side));
        return env.atlas.sample(4, xy / make_float2(6.f * env.face_size, 2.f * env.face_size)).as_vec4().xyz();
    };
    Float level = clamp(mip, 0.f, float(env.levels - 1));
    Float lo = floor(level);
    Float3 first = sample_level(lo);
    Float3 second = sample_level(min(lo + 1.f, float(env.levels - 1)));
    return pow(abs(lerp(level - lo, first, second)), make_float3(2.2f));
}

static Float3 direct_brdf(Float3 n, Float3 v, Float3 l, Float3 base, Float3 spec, Float4 m0, Float4 m1, Float4 m2,
                   Float gloss) {
    // Each chart instance stores three material vectors followed by its placement and gloss.
    Float metallic = m0.w, roughness = max(.001f, m1.x), subsurface = m1.w;
    Float anisotropy = m2.x, sheen_strength = m2.y, sheen_tint = m2.z, clearcoat = m2.w;
    constexpr float pi = 3.14159265f;
    Float3 h = normalize(v + l);
    Float nl = max(dot(n, l), 0.f), nv = max(dot(n, v), 0.f), nh = max(dot(n, h), 0.f);
    Float lh = clamp(dot(l, h), 0.f, 1.f);
    Float fl = pow(1.f - nl, 5.f), fv = pow(1.f - nv, 5.f), fh = pow(1.f - lh, 5.f);
    Float fd90 = .5f + 2.f * lh * lh * roughness;
    Float fd = lerp(fl, 1.f, fd90) * lerp(fv, 1.f, fd90);
    Float fss90 = lh * lh * roughness;
    Float fss = lerp(fl, 1.f, fss90) * lerp(fv, 1.f, fss90);
    Float ss = 1.25f * (fss * (1.f / max(nl + nv, .0001f) - .5f) + .5f);
    Float3 tint = base / max(dot(base, make_float3(.3f, .6f, .1f)), .0001f);
    Float3 sheen = fh * sheen_strength * lerp(sheen_tint, make_float3(1.f), tint);
    Float3 diffuse = (1.f - metallic) * (base / pi * lerp(subsurface, fd, ss) + sheen);

    Float3 tangent = cross(n, make_float3(0.f, 1.f, 0.f));
    $if (dot(tangent, tangent) < .0001f) {
        tangent = cross(n, make_float3(1.f, 0.f, 0.f));
    };
    tangent = normalize(tangent);
    Float3 bitangent = cross(n, tangent);
    Float aspect = sqrt(1.f - .9f * anisotropy);
    Float ax = max(.001f, roughness * roughness / aspect), ay = max(.001f, roughness * roughness * aspect);
    Float q = sqr(dot(h, tangent) / ax) + sqr(dot(h, bitangent) / ay) + nh * nh;
    Float distribution = 1.f / (pi * ax * ay * q * q);
    auto smith = [&](Float3 w, Float nw) {
        return 1.f / max(.0001f, nw + sqrt(sqr(dot(w, tangent) * ax) + sqr(dot(w, bitangent) * ay) + nw * nw));
    };
    Float3 reflection = lerp(fh, spec, make_float3(1.f)) * distribution * smith(v, nv) * smith(l, nl);

    Float a2 = sqr(lerp(gloss, .1f, .001f));
    Float coat_d = (a2 - 1.f) / (pi * log(a2) * (1.f + (a2 - 1.f) * nh * nh));
    auto coat_g = [](Float nw) { return 1.f / (nw + sqrt(.0625f + .9375f * nw * nw)); };
    Float coat = .25f * clearcoat * coat_d * lerp(fh, .04f, 1.f) * coat_g(nl) * coat_g(nv);
    return diffuse + reflection + coat;
}

static ocarina::Device open_device() {
    // Slang builtins and the dynamically loaded backend live next to the executable.
#ifdef _WIN32
    std::wstring filename(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, filename.data(), static_cast<DWORD>(filename.size()));
    if (!length || length >= filename.size())
        throw std::runtime_error("Cannot locate HorizonExamples");
    filename.resize(length);
    const auto executable = std::filesystem::path(filename);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string filename(size, '\0');
    if (_NSGetExecutablePath(filename.data(), &size))
        throw std::runtime_error("Cannot locate HorizonExamples");
    const auto executable = std::filesystem::canonical(filename.c_str());
#else
    const auto executable = std::filesystem::read_symlink("/proc/self/exe");
#endif
    auto& context = ocarina::RHIContext::instance(executable.parent_path());
    auto device = context.create_device();
    if (!device.supports_ray_queries())
        throw std::runtime_error("Ocarina ray queries unsupported on this device");
    device.init_rtx();
    return device;
}

DisneyRayDisplay::DisneyRayDisplay(GLFWwindow* window)
    : window_(window), surface_(native_handle(window), PresentDevicePreference::RenderDevice) {}

void DisneyRayDisplay::present(std::span<const uint32_t> pixels) {
    // The compute device and Horizon presenter can be on different adapters.
    // Wait before reusing the upload buffer that the previous frame still reads.
    copy_.waitForDeferredResources();
    if (pixels.size() != image_width * image_height || !staging_.copyFromData(pixels.data(), pixels.size_bytes()))
        throw std::runtime_error("Ray image upload failed");
    copy_ << staging_.copyTo(image_);
    const bool shown = surface_.present(copy_, image_);

    if (shown && remaining_frames_ > 0 && --remaining_frames_ == 0)
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    if (!glfwWindowShouldClose(window_)) return;
    const char* path = std::getenv("HORIZON_CAPTURE_PATH");
    if (!path || !*path) return;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    if (!stbi_write_png(path, image_width, image_height, 4, pixels.data(), image_width * 4))
        throw std::runtime_error("Could not write ray-traced PNG");
}

static int env_int(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    char* end{};
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end || parsed < 0 || parsed > 10000000)
        throw std::invalid_argument(std::string("Invalid ") + name);
    return static_cast<int>(parsed);
}
