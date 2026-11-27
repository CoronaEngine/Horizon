#include "DisneyEdsl.h"

#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <vector>

DisneyEdsl::DisneyEdsl(void* native_window)
    : ExampleBase(native_window), display(native_window)
{
    constexpr int width = 800, height = 600;
    // Upload the six faces and all prefiltered mips of each environment map.
    const auto environment_dir = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "env";
    for (const auto& [filename, face_size, mip_count, cubemap] : {
            std::tuple{"bolonga_lod.dds", 256u, 9u, &radiance_image},
            std::tuple{"bolonga_irr.dds", 128u, 1u, &irradiance_image}}) {
        uint32_t texels_per_face = 0;
        for (uint32_t mip = 0; mip < mip_count; ++mip) {
            const uint32_t dim = face_size >> mip;
            texels_per_face += dim * dim;
        }
        std::vector<uint64_t> texels(6 * texels_per_face);
        std::ifstream file(environment_dir / filename, std::ios::binary);
        file.seekg(148); // The bundled DX10 DDS files store RGBA16F texels after the header.
        file.read(reinterpret_cast<char*>(texels.data()), texels.size() * sizeof(uint64_t));

        HardwareImage::CreateInfo desc(face_size, face_size, HardwareImage::Format::RGBA16_FLOAT);
        desc.arrayLayers = 6;
        desc.cube = true;
        desc.linearFiltering = true;
        desc.mipLevels = static_cast<int>(mip_count);
        *cubemap = HardwareImage(desc);
        HardwareBuffer staging(texels, HardwareBuffer::Usage::StorageBuffer);
        uint64_t offset = 0;
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t mip = 0; mip < mip_count; ++mip) {
                const uint32_t dim = face_size >> mip;
                render_executor << staging.copyTo(*cubemap, offset, face, mip);
                offset += static_cast<uint64_t>(dim) * dim * sizeof(uint64_t);
            }
        }
    }

    // Each triangle-strip vertex also supplies the sphere's object-space normal.
    {
        constexpr int slices = 48, stacks = 24;
        std::vector<std::array<float, 3>> vertices;
        std::vector<uint16_t> indices;
        vertices.reserve(static_cast<size_t>((slices + 1) * (stacks + 1)));
        indices.reserve(static_cast<size_t>(stacks * (slices + 1) * 2 + (stacks - 1) * 2));

        for (int y = 0; y <= stacks; ++y) {
            const float phi = (static_cast<float>(y) / stacks) * ktm::pi<float>;
            const float sin_phi = ktm::sin(phi);
            const float cos_phi = ktm::cos(phi);

            for (int x = 0; x <= slices; ++x) {
                const float theta = (static_cast<float>(x) / slices) * ktm::two_pi<float>;
                vertices.push_back({ sin_phi * ktm::cos(theta), cos_phi, sin_phi * ktm::sin(theta) });
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

        render_executor << vertex_buffer.createDeviceLocal(HardwareBuffer::Usage::VertexBuffer,
            vertices.data(), vertices.size(), sizeof(vertices[0]));
        render_executor << index_buffer.createDeviceLocal(HardwareBuffer::Usage::IndexBuffer,
            indices.data(), indices.size(), sizeof(indices[0]));

        constexpr uint32_t parameter_words = sizeof(DrawParameters) / sizeof(uint32_t);
        std::array<RasterIndirectPacket, rows * columns> packets;
        for (uint32_t i = 0; i < packets.size(); ++i) {
            packets[i] = {.vertexCount = static_cast<uint32_t>(indices.size()), .instanceCount = 1,
                          .parameterWord = i * parameter_words};
        }
        render_executor << commands.createDeviceLocal(HardwareBuffer::Usage::RasterIndirect,
            packets.data(), packets.size(), sizeof(packets[0]));
    }
    std::array<DrawParameters, rows * columns> parameters;
    constexpr float radius = 0.42f, spacing = 1.12f;
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
            auto& draw = parameters[row * columns + col];
            draw = column[row];
            draw.model = ktm::translate3d(ktm::fvec3(x, y, 0.0f)) * ktm::scale3d(ktm::fvec3(radius));
        }
    }
    render_executor << parameter_buffer.createDeviceLocal(HardwareBuffer::Usage::StorageBuffer,
        parameters.data(), parameters.size(), sizeof(DrawParameters));
    render_executor.commit().wait();

    using EmbeddedShader::Float, EmbeddedShader::Float3, EmbeddedShader::Float4;
    using EmbeddedShader::GPU_IF, EmbeddedShader::GPU_IF_BRANCH;
    using EmbeddedShader::abs;
    using EmbeddedShader::clamp;
    using EmbeddedShader::cross;
    using EmbeddedShader::dot;
    using EmbeddedShader::exp2;
    using EmbeddedShader::max;
    using EmbeddedShader::mix;
    using EmbeddedShader::mul;
    using EmbeddedShader::normalize;
    using EmbeddedShader::pow;
    using EmbeddedShader::rasterDrawParameter;
    using EmbeddedShader::sqrt;

    final_output_image = HardwareImage(width, height, HardwareImage::Format::RGBA16_FLOAT);
    final_output_image.setClearColor(0.5f, 0.5f, 0.5f, 1.0f);

    auto edsl_vertex = [&](Float3 pos) {
        auto per_model = rasterDrawParameter<ktm::fmat4x4>(offsetof(DrawParameters, model) / sizeof(uint32_t));

        EmbeddedShader::Aggregate<Varyings> out;

        Float4 local_pos = Float4(pos, Float(1.0));
        Float4 world_pos = mul(per_model, local_pos);
        EmbeddedShader::position() = mul(mul(proj_view, per_model), local_pos);
        out->v_view = camPos->xyz() - world_pos->xyz();
        out->v_normal = mul(per_model, Float4(pos, Float(0.0)))->xyz();
        return out;
    };

    auto edsl_fragment = [&](EmbeddedShader::Aggregate<Varyings> in) {
        Float4 per_mat0;
        per_mat0 = rasterDrawParameter<ktm::fvec4>(offsetof(DrawParameters, baseColorMetallic) / sizeof(uint32_t));
        Float4 per_mat1;
        per_mat1 = rasterDrawParameter<ktm::fvec4>(offsetof(DrawParameters, roughness) / sizeof(uint32_t));
        Float4 per_mat2;
        per_mat2 = rasterDrawParameter<ktm::fvec4>(offsetof(DrawParameters, anisotropic) / sizeof(uint32_t));

        Float3 N = normalize(in->v_normal);
        Float3 V = normalize(in->v_view);
        Float3 L = normalize(Float3(lightDir->xyz()));

        Float3 X;
        X = cross(N, Float3(Float(0.0), Float(1.0), Float(0.0)));
        $IF(dot(X, X) < Float(1e-4))
        {
            X = cross(N, Float3(Float(1.0), Float(0.0), Float(0.0)));
        };
        X = normalize(X);
        Float3 Y = normalize(cross(N, X));

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
        Float clearcoatGloss = rasterDrawParameter<float>(offsetof(DrawParameters, clearcoatGloss) / sizeof(uint32_t));

        const auto toLinear = [](Float3 rgb) {
            return pow(abs(rgb), Float3(2.2f));
        };
        Float3 Cdlin = toLinear(baseColor);
        Float Cdlum = dot(Cdlin, Float3(Float(0.3), Float(0.6), Float(0.1)));
        Float3 Ctint(1.0f);
        $IF(Cdlum > Float(0.0)) { Ctint = Cdlin / Cdlum; };
        Float3 Cspec0 =
            mix(specular * Float(0.08) * mix(Float3(1.0f), Ctint, specularTint), Cdlin, metallic);
        Float3 Csheen = mix(Float3(1.0f), Ctint, sheenTint);

        Float NdotV = dot(N, V);
        const auto schlickFresnel = [](Float u) {
            Float m = clamp(Float(1.0) - u, Float(0.0), Float(1.0));
            return m * m * m * m * m;
        };
        Float envFresnel = mix(Cspec0->x, Float(1.0), schlickFresnel(NdotV));

        Float3 brdf(0.0f);
        Float NdotL = dot(N, L);
        $IF((NdotL >= Float(0.0)) && (NdotV >= Float(0.0)))
        {
            Float3 H = normalize(L + V);
            Float NdotH;
            NdotH = dot(N, H);
            Float LdotH = dot(L, H);

            Float FL = schlickFresnel(NdotL);
            Float FV = schlickFresnel(NdotV);
            Float Fd90 = Float(0.5) + Float(2.0) * LdotH * LdotH * roughness;
            Float Fd = mix(Float(1.0), Fd90, FL) * mix(Float(1.0), Fd90, FV);

            Float Fss90 = LdotH * LdotH * roughness;
            Float Fss = mix(Float(1.0), Fss90, FL) * mix(Float(1.0), Fss90, FV);
            Float ss = Float(1.25) * (Fss * (Float(1.0) / (NdotL + NdotV) - Float(0.5)) + Float(0.5));

            const auto sqr = [](Float x) { return x * x; };
            Float aniso_aspect = sqrt(Float(1.0) - anisotropic * Float(0.9));
            Float ax = max(Float(0.001), sqr(roughness) / aniso_aspect);
            Float ay = max(Float(0.001), sqr(roughness) * aniso_aspect);
            Float Ds = Float(1.0) /
                (Float(3.14159265358979323846) * ax * ay *
                 sqr(sqr(dot(H, X) / ax) + sqr(dot(H, Y) / ay) + NdotH * NdotH));
            Float FH = schlickFresnel(LdotH);
            Float3 Fs = mix(Cspec0, Float3(1.0f), FH);
            const auto smithGGXAniso = [&](Float n, Float x, Float y) {
                return Float(1.0) / (n + sqrt(sqr(x * ax) + sqr(y * ay) + sqr(n)));
            };
            Float Gs = smithGGXAniso(NdotL, dot(L, X), dot(L, Y)) *
                       smithGGXAniso(NdotV, dot(V, X), dot(V, Y));

            Float3 Fsheen = FH * sheen * Csheen;

            Float clearcoatAlpha =
                Float(0.1) * (Float(1.0) - clearcoatGloss) + Float(0.001) * clearcoatGloss;
            Float a2 = clearcoatAlpha * clearcoatAlpha;
            Float t = Float(1.0) + (a2 - Float(1.0)) * NdotH * NdotH;
            Float Dr = (a2 - Float(1.0)) / (Float(3.14159265358979323846) * EmbeddedShader::log(a2) * t);
            Float Fr = Float(0.04) * (Float(1.0) - FH) + Float(1.0) * FH;
            const auto smithGGXClearcoat = [](Float n) {
                Float a = Float(0.25) * Float(0.25);
                Float b = n * n;
                return Float(1.0) / (n + sqrt(a + b - a * b));
            };
            Float Gr = smithGGXClearcoat(NdotL) * smithGGXClearcoat(NdotV);

            Float3 diffusePart =
                ((Float(1.0) / Float(3.14159265358979323846)) * mix(Fd, ss, subsurface) * Cdlin + Fsheen) *
                (Float(1.0) - metallic);
            Float3 specPart = Gs * Fs * Ds + Float(0.25) * clearcoat * Gr * Fr * Dr;
            brdf = diffusePart + specPart;
        };

        Float3 direct = brdf * clamp(NdotL, Float(0.0), Float(1.0)) * Float(2.0);

        Float mip = roughness * Float(5.0);
        Float3 vr = normalize(Float(2.0) * NdotV * N - V);
        Float3 reflected = normalize(vr);
        Float ax = abs(reflected->x), ay = abs(reflected->y), az = abs(reflected->z);
        Float major = max(max(ax, ay), az);
        Float scale = Float(1.0) - exp2(mip) / Float(256.0);
        Float3 cubeR;
        cubeR = reflected;
        $IF(ax != major) { cubeR->x = reflected->x * scale; };
        $IF(ay != major) { cubeR->y = reflected->y * scale; };
        $IF(az != major) { cubeR->z = reflected->z * scale; };

        Float3 radiance = toLinear(Float3(EmbeddedShader::textureLod(radiance_map, cubeR, mip)->xyz()));
        Float3 irradiance = toLinear(Float3(EmbeddedShader::texture(irradiance_map, normalize(N))->xyz()));

        Float3 diffuseAlbedo = Cdlin * (Float(1.0) - metallic);
        Float3 indirect = diffuseAlbedo * irradiance + Cspec0 * radiance * envFresnel;

        Float3 mapped = max(Float3(0.0f), (direct + indirect) - Float3(0.004f));
        Float3 filmic = (mapped * (Float(6.2) * mapped + Float3(0.5f))) /
                        (mapped * (Float(6.2) * mapped + Float3(1.7f)) + Float3(0.06f));
        final_output << Float4(filmic, Float(1.0));
    };
    raster = RasterPipeline(edsl_vertex, edsl_fragment, {.topology = RasterTopology::TriangleStrip},
                            {.enableMatrixColumnMajor = true});

    constexpr float yaw = ktm::pi<float>, pitch = 0.42f * ktm::pi<float>;
    const auto eye = 13.5f * ktm::fvec3(ktm::sin(pitch) * ktm::sin(yaw), ktm::cos(pitch), ktm::sin(pitch) * ktm::cos(yaw));
    const auto view = ktm::look_at_lh(eye, ktm::fvec3(0.0f), ktm::fvec3(0, 1, 0));
    auto projection = ktm::perspective_lh(ktm::radians(45.0f), static_cast<float>(width) / height, 0.1f, 100.0f);
    projection[1][1] *= -1.0f;
    proj_view = projection * view;
    camPos = ktm::fvec4(eye, 1.0f);
    lightDir = ktm::fvec4(ktm::normalize(ktm::fvec3(-0.35f, 0.55f, -0.75f)), 0.0f);
}

void DisneyEdsl::run(float)
{
    render_executor << raster(vertex_buffer, index_buffer, commands, rows * columns, parameter_buffer)
                    << display(final_output_image);
    render_executor.commit();
}
