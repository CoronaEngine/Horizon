#include "Horizon.h"
#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"

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
#include <bit>
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
constexpr uint32_t disney_width = 1920;
constexpr uint32_t disney_height = 1080;
constexpr int chart_row_count = 10;
constexpr int chart_col_count = 11;

struct DisneyEdslSphereParameters {
    glm::mat4 model;
    glm::vec4 mat0, mat1, mat2;
    float clearcoatGloss;
};
static_assert(offsetof(DisneyEdslSphereParameters, clearcoatGloss) == 28 * sizeof(uint32_t));
static_assert(sizeof(DisneyEdslSphereParameters) == 29 * sizeof(uint32_t));

static RasterGeometry make_sphere();
static DisneyEdslSphereParameters sphere_parameters(int row, int col);
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

using EmbeddedShader::Float;
using EmbeddedShader::Float3;
using EmbeddedShader::Float4;
using EmbeddedShader::GPU_IF;
using EmbeddedShader::GPU_IF_BRANCH;
using EmbeddedShader::abs;
using EmbeddedShader::clamp;
using EmbeddedShader::dot;
using EmbeddedShader::exp2;
using EmbeddedShader::max;
using EmbeddedShader::mix;
using EmbeddedShader::mul;
using EmbeddedShader::normalize;
using EmbeddedShader::pow;
using EmbeddedShader::sqrt;

struct DisneyEdslVaryings
{
    Float3 v_view;
    Float3 v_normal;
};

struct DisneyEdslSharedProxy
{
    EmbeddedShader::Float4x4 proj_view;
    EmbeddedShader::Float4x4 envMtx;
    Float4 camPos;
    Float4 flags;
    Float4 lightDir;
    Float4 lightCol;
    Float4 exposurePad;
};

static Float3 toLinear(Float3 rgb) {
    return pow(abs(rgb), Float3(Float(2.2), Float(2.2), Float(2.2)));
}

static Float3 toFilmic(Float3 rgb) {
    Float3 mapped = max(Float3(Float(0.0), Float(0.0), Float(0.0)),
                        rgb - Float3(Float(0.004), Float(0.004), Float(0.004)));
    return (mapped * (Float(6.2) * mapped + Float3(Float(0.5), Float(0.5), Float(0.5)))) /
           (mapped * (Float(6.2) * mapped + Float3(Float(1.7), Float(1.7), Float(1.7))) +
            Float3(Float(0.06), Float(0.06), Float(0.06)));
}

static Float3 fixCubeLookup(Float3 v, Float lod, Float topLevelCubeSize) {
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
}

static Float sqr(Float x) { return x * x; }

static Float schlickFresnel(Float u) {
    Float m = clamp(Float(1.0) - u, Float(0.0), Float(1.0));
    return m * m * m * m * m;
}

static Float edsl_log(Float x) {
    return Float { EmbeddedShader::Ast::AST::callFunc(
        "log", EmbeddedShader::Ast::AST::createType<float>(),
        EmbeddedShader::Ast::Node::accessAll({ EmbeddedShader::proxy_wrap(x) }, EmbeddedShader::Ast::AccessPermissions::ReadOnly)) };
}

static Float3 edsl_cross(Float3 a, Float3 b) {
    return Float3 { EmbeddedShader::Ast::AST::callFunc(
        "cross", EmbeddedShader::Ast::AST::createType<ktm::fvec3>(),
        EmbeddedShader::Ast::Node::accessAll({ EmbeddedShader::proxy_wrap(a), EmbeddedShader::proxy_wrap(b) }, EmbeddedShader::Ast::AccessPermissions::ReadOnly)) };
}

static Float gtr1(Float NdotH, Float a) {
    Float a2 = a * a;
    Float t = Float(1.0) + (a2 - Float(1.0)) * NdotH * NdotH;
    Float result;
    result = (a2 - Float(1.0)) / (Float(3.14159265358979323846) * edsl_log(a2) * t);
    $IF(a >= Float(1.0)) { result = Float(1.0) / Float(3.14159265358979323846); };
    return result;
}

static Float gtr2_aniso(Float NdotH, Float HdotX, Float HdotY, Float ax, Float ay) {
    return Float(1.0) /
           (Float(3.14159265358979323846) * ax * ay *
            sqr(sqr(HdotX / ax) + sqr(HdotY / ay) + NdotH * NdotH));
}

static Float smithG_GGX(Float NdotV, Float alphaG) {
    Float a = alphaG * alphaG;
    Float b = NdotV * NdotV;
    return Float(1.0) / (NdotV + sqrt(a + b - a * b));
}

static Float smithG_GGX_aniso(Float NdotV, Float VdotX, Float VdotY, Float ax, Float ay) {
    return Float(1.0) / (NdotV + sqrt(sqr(VdotX * ax) + sqr(VdotY * ay) + sqr(NdotV)));
}

static auto open_window(int width, int height, const char* title) {
    const bool hidden = env_int("HORIZON_BENCH_HIDDEN") != 0;
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

void run_example_disney_edsl()
{
    constexpr auto mode = "disney_edsl";
    auto window = open_window(disney_width, disney_height, mode);

    HardwareImage radiance = load_cubemap("bolonga_lod.dds");
    HardwareImage irradiance = load_cubemap("bolonga_irr.dds");
    auto sphere = make_sphere();

    HardwareImage final_output_image(disney_width, disney_height, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
    final_output_image.setClearColor(0.5f, 0.5f, 0.5f, 1.0f);

    DisneyEdslSharedProxy shared;
    EmbeddedShader::TextureCube<ktm::fvec4> texCube, texCubeIrr;
    EmbeddedShader::Texture2D<ktm::fvec4> final_output;
    bool directEnabled = true;
    bool iblEnabled = true;

    auto edsl_vertex = [&](Float3 pos) {
        auto per_model = EmbeddedShader::rasterDrawParameter<ktm::fmat4x4>(0);

        EmbeddedShader::Aggregate<DisneyEdslVaryings> out;

        Float4 local_pos = Float4(pos, Float(1.0));
        Float4 world_pos = mul(per_model, local_pos);
        EmbeddedShader::position() = mul(mul(shared.proj_view, per_model), local_pos);
        out->v_view = shared.camPos->xyz() - world_pos->xyz();
        out->v_normal = mul(per_model, Float4(pos, Float(0.0)))->xyz();
        return out;
    };

    // x: doDiffuse, y: doSpecular, z: doDiffuseIbl, w: doSpecularIbl
    auto edsl_fragment = [&](EmbeddedShader::Aggregate<DisneyEdslVaryings> in) {
        Float4 per_mat0;
        per_mat0 = EmbeddedShader::rasterDrawParameter<ktm::fvec4>(16);
        Float4 per_mat1;
        per_mat1 = EmbeddedShader::rasterDrawParameter<ktm::fvec4>(20);
        Float4 per_mat2;
        per_mat2 = EmbeddedShader::rasterDrawParameter<ktm::fvec4>(24);

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
        Float clearcoatGloss = EmbeddedShader::rasterDrawParameter<float>(28);

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
        $IF(directEnabled)
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
        indirect = Float3(0.0f);
        $IF(iblEnabled)
        {
            Float mip = roughness * Float(5.0);
            Float3 vr = normalize(Float(2.0) * NdotVraw * N - V);
            Float3 cubeR = fixCubeLookup(normalize(Float3(mul(shared.envMtx, Float4(vr, Float(0.0)))->xyz())), mip,
                                         Float(256.0));
            Float3 cubeN = normalize(Float3(mul(shared.envMtx, Float4(N, Float(0.0)))->xyz()));

            Float3 radiance = toLinear(Float3(EmbeddedShader::textureLod(texCube, cubeR, mip)->xyz()));
            Float3 irradiance = toLinear(Float3(EmbeddedShader::texture(texCubeIrr, cubeN)->xyz()));

            Float3 diffuseAlbedo = Cdlin * (Float(1.0) - metallic);
            Float3 envDiffuse = diffuseAlbedo * irradiance * shared.flags->z;
            Float3 envSpecular = Cspec0 * radiance * envFresnel * shared.flags->w;
            indirect = envDiffuse + envSpecular;
        }

        Float3 color = (direct + indirect) * exp2(shared.exposurePad->x);
        final_output << Float4(toFilmic(color), Float(1.0));
    };
    RasterizerPipeline<> rasterizer(edsl_vertex, edsl_fragment, 1, EmbeddedShader::CompilerOption{.enableMatrixColumnMajor = true});
    rasterizer.setRasterState({.topology = RasterTopology::TriangleStrip});

    HardwareExecutor render_executor;
    HardwareDisplayer display(native_handle(window.get()), PresentDevicePreference::RenderDevice);

    constexpr float aspect = static_cast<float>(disney_width) / disney_height;
    auto projection = glm::perspectiveLH(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    projection[1][1] *= -1.0f;
    const glm::vec3 target(0.0f);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    constexpr float yaw = glm::pi<float>(), pitch = 0.42f * glm::pi<float>();
    const glm::vec3 direction(std::sin(pitch) * std::sin(yaw), std::cos(pitch), std::sin(pitch) * std::cos(yaw));
    const auto eye = target + direction * 13.5f;
    const auto proj_view = projection * glm::lookAtLH(eye, target, up);
    const glm::vec4 camera_position(eye, 1.0f);
    const glm::vec4 light_direction(glm::normalize(glm::vec3(-0.35f, 0.55f, -0.75f)), 0.0f);
    texCube = radiance;
    texCubeIrr = irradiance;
    shared.proj_view = std::bit_cast<ktm::fmat4x4>(proj_view);
    shared.camPos = std::bit_cast<ktm::fvec4>(camera_position);
    shared.lightDir = std::bit_cast<ktm::fvec4>(light_direction);
    shared.lightCol = std::bit_cast<ktm::fvec4>(glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    shared.envMtx = std::bit_cast<ktm::fmat4x4>(glm::mat4(1.0f));
    shared.exposurePad = std::bit_cast<ktm::fvec4>(glm::vec4(0.0f));
    final_output = final_output_image;

    int remaining_frames = env_int("HORIZON_PRESENT_FRAMES");
    const bool offscreen = env_int("HORIZON_BENCH_OFFSCREEN") != 0;
    float fixed_dt = 0.0f;
    if (const char* value = std::getenv("HORIZON_FIXED_DT"); value && *value) {
        fixed_dt = std::stof(value);
        if (!std::isfinite(fixed_dt) || fixed_dt < 0) throw std::invalid_argument("Invalid HORIZON_FIXED_DT");
    }
    double previous = glfwGetTime();
    float phase_seconds = 0.0f;
    int phase = 0;
    while (!glfwWindowShouldClose(window.get()))
    {
        const double now = glfwGetTime();
        phase_seconds += fixed_dt > 0 ? fixed_dt : static_cast<float>(now - previous);
        previous = now;
        glfwPollEvents();
        if (phase_seconds >= 1.5f) {
            phase_seconds = 0.0f;
            phase = (phase + 1) & 3;
        }
        directEnabled = (phase & 1) == 0;
        iblEnabled = (phase & 2) == 0;
        shared.flags = std::bit_cast<ktm::fvec4>(glm::vec4(directEnabled, directEnabled, iblEnabled, iblEnabled));
        auto batch = rasterizer.beginBatch();

        for (int row = 0; row < chart_row_count; ++row)
        {
            for (int col = 0; col < chart_col_count; ++col)
            {
                const auto draw = sphere_parameters(row, col);
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
    if (const char* filename = std::getenv("HORIZON_CAPTURE_PATH"); filename && *filename) {
        const std::filesystem::path path(filename);
        const uint32_t byte_count = disney_width * disney_height * 8;
        HardwareBuffer readback(byte_count, BufferUsage::StorageBuffer);
        HardwareExecutor copy;
        copy.wait(render_executor);
        copy << final_output_image.copyTo(readback);
        copy.commit().waitForDeferredResources();
        std::vector<uint16_t> half(static_cast<size_t>(disney_width) * disney_height * 4);
        if (!readback.copyToData(half.data(), byte_count)) throw std::runtime_error("Capture readback failed");
        std::vector<unsigned char> rgba(half.size());
        for (size_t i = 0; i < half.size(); ++i)
            rgba[i] = static_cast<unsigned char>(std::clamp(glm::unpackHalf1x16(half[i]), 0.0f, 1.0f) * 255.0f + 0.5f);
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        if (!stbi_write_png(path.string().c_str(), disney_width, disney_height, 4, rgba.data(), disney_width * 4))
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

static DisneyEdslSphereParameters sphere_parameters(int row, int col)
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
    DisneyEdslSphereParameters draw{};
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
