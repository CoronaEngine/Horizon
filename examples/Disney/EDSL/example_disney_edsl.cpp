#include "Horizon.h"
#include "../scene.h"
#include "window.h"
#include "example_benchmark/benchmark.h"
#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"
#include <bit>

#include <GLFW/glfw3.h>
#include <utility>
#include <cstdio>

namespace disney_edsl {
using namespace EmbeddedShader;

struct DisneyEdslVaryings
{
    Float3 v_view;
    Float3 v_normal;
};

struct DisneyEdslSharedProxy
{
    Float4x4 proj_view;
    Float4x4 envMtx;
    Float4 camPos;
    Float4 flags;
    Float4 lightDir;
    Float4 lightCol;
    Float4 exposurePad;
};

Float3 toLinear(Float3 rgb) {
    return pow(abs(rgb), Float3(Float(2.2), Float(2.2), Float(2.2)));
}

Float3 toFilmic(Float3 rgb) {
    Float3 mapped = max(Float3(Float(0.0), Float(0.0), Float(0.0)),
                        rgb - Float3(Float(0.004), Float(0.004), Float(0.004)));
    return (mapped * (Float(6.2) * mapped + Float3(Float(0.5), Float(0.5), Float(0.5)))) /
           (mapped * (Float(6.2) * mapped + Float3(Float(1.7), Float(1.7), Float(1.7))) +
            Float3(Float(0.06), Float(0.06), Float(0.06)));
}

Float3 fixCubeLookup(Float3 v, Float lod, Float topLevelCubeSize) {
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

Float sqr(Float x) { return x * x; }

Float schlickFresnel(Float u) {
    Float m = clamp(Float(1.0) - u, Float(0.0), Float(1.0));
    return m * m * m * m * m;
}

Float edsl_log(Float x) {
    return Float { Ast::AST::callFunc(
        "log", Ast::AST::createType<float>(),
        Ast::Node::accessAll({ proxy_wrap(x) }, Ast::AccessPermissions::ReadOnly)) };
}

Float3 edsl_cross(Float3 a, Float3 b) {
    return Float3 { Ast::AST::callFunc(
        "cross", Ast::AST::createType<ktm::fvec3>(),
        Ast::Node::accessAll({ proxy_wrap(a), proxy_wrap(b) }, Ast::AccessPermissions::ReadOnly)) };
}

Float gtr1(Float NdotH, Float a) {
    Float a2 = a * a;
    Float t = Float(1.0) + (a2 - Float(1.0)) * NdotH * NdotH;
    Float result;
    result = (a2 - Float(1.0)) / (Float(3.14159265358979323846) * edsl_log(a2) * t);
    $IF(a >= Float(1.0)) { result = Float(1.0) / Float(3.14159265358979323846); };
    return result;
}

Float gtr2_aniso(Float NdotH, Float HdotX, Float HdotY, Float ax, Float ay) {
    return Float(1.0) /
           (Float(3.14159265358979323846) * ax * ay *
            sqr(sqr(HdotX / ax) + sqr(HdotY / ay) + NdotH * NdotH));
}

Float smithG_GGX(Float NdotV, Float alphaG) {
    Float a = alphaG * alphaG;
    Float b = NdotV * NdotV;
    return Float(1.0) / (NdotV + sqrt(a + b - a * b));
}

Float smithG_GGX_aniso(Float NdotV, Float VdotX, Float VdotY, Float ax, Float ay) {
    return Float(1.0) / (NdotV + sqrt(sqr(VdotX * ax) + sqr(VdotY * ay) + sqr(NdotV)));
}
} // namespace disney_edsl

void run_example_disney_edsl()
{
    using namespace disney;
    using namespace EmbeddedShader;
    using namespace ktm;
    using namespace disney_edsl;
    constexpr auto mode = "disney_edsl";
    auto window = examples::open_window(disney_width, disney_height, mode);

    InputContext input;
    attach_input(window.get(), input);

    HardwareImage radiance = load_cubemap("bolonga_lod.dds");
    HardwareImage irradiance = load_cubemap("bolonga_irr.dds");
    auto sphere = make_sphere();

    HardwareImage final_output_image(disney_width, disney_height, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
    final_output_image.setClearColor(0.5f, 0.5f, 0.5f, 1.0f);

    HardwareImage depth_image(disney_width, disney_height, ImageFormat::D32_FLOAT, ImageUsage::DepthImage);

    const auto shader_start = bench::Clock::now();
    DisneyEdslSharedProxy shared;
    TextureCube<fvec4> texCube, texCubeIrr;
    Texture2D<fvec4> final_output;
    bool directEnabled = true;
    bool iblEnabled = true;

    auto edsl_vertex = [&](Float3 pos) {
        auto per_model = rasterDrawParameter<fmat4x4>(0);

        Aggregate<DisneyEdslVaryings> out;

        Float4 local_pos = Float4(pos, Float(1.0));
        Float4 world_pos = mul(per_model, local_pos);
        position() = mul(mul(shared.proj_view, per_model), local_pos);
        out->v_view = shared.camPos->xyz() - world_pos->xyz();
        out->v_normal = mul(per_model, Float4(pos, Float(0.0)))->xyz();
        return out;
    };

    // x: doDiffuse, y: doSpecular, z: doDiffuseIbl, w: doSpecularIbl
    auto edsl_fragment = [&](Aggregate<DisneyEdslVaryings> in) {
        Float4 per_mat0;
        per_mat0 = rasterDrawParameter<fvec4>(16);
        Float4 per_mat1;
        per_mat1 = rasterDrawParameter<fvec4>(20);
        Float4 per_mat2;
        per_mat2 = rasterDrawParameter<fvec4>(24);

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
        Float clearcoatGloss = rasterDrawParameter<float>(28);

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

            Float3 radiance = toLinear(Float3(textureLod(texCube, cubeR, mip)->xyz()));
            Float3 irradiance = toLinear(Float3(texture(texCubeIrr, cubeN)->xyz()));

            Float3 diffuseAlbedo = Cdlin * (Float(1.0) - metallic);
            Float3 envDiffuse = diffuseAlbedo * irradiance * shared.flags->z;
            Float3 envSpecular = Cspec0 * radiance * envFresnel * shared.flags->w;
            indirect = envDiffuse + envSpecular;
        }

        Float3 color = (direct + indirect) * exp2(shared.exposurePad->x);
        final_output << Float4(toFilmic(color), Float(1.0));
    };
    RasterizerPipeline<> rasterizer(edsl_vertex, edsl_fragment, 1, CompilerOption{.enableMatrixColumnMajor = true});
    const double shader_setup_ms = bench::milliseconds_since(shader_start);
    rasterizer.setDepthImage(depth_image);
    rasterizer.setRasterState({.topology = RasterTopology::TriangleStrip});

    HardwareExecutor render_executor;
    HardwareDisplayer display(examples::native_handle(window.get()), PresentDevicePreference::RenderDevice);

    auto run = bench::start_run(mode, disney_width, disney_height, shader_setup_ms);
    float branch_switch_accum = 0.0f;
    int branch_switch_state = 0;
    while (!glfwWindowShouldClose(window.get()))
    {
        bench::begin_frame(run);
        glfwPollEvents();
        Settings& s = input.settings;

        const float dt = run.delta_seconds;
        branch_switch_accum += dt;
        if (branch_switch_accum >= 1.5f)
        {
            branch_switch_accum = 0.0f;
            branch_switch_state = (branch_switch_state + 1) & 3;
            const bool direct_on = (branch_switch_state & 1) == 0;
            const bool ibl_on = (branch_switch_state & 2) == 0;
            s.do_diffuse = direct_on;
            s.do_specular = direct_on;
            s.do_diffuse_ibl = ibl_on;
            s.do_specular_ibl = ibl_on;
            std::printf("[edsl_branch_switch] state=%d direct=%d ibl=%d\n",
                        branch_switch_state, direct_on ? 1 : 0, ibl_on ? 1 : 0);
            std::fflush(stdout);
        }

        const auto frame = update_frame(input, dt);

        const auto record_start = bench::Clock::now();
        texCube = radiance;
        texCubeIrr = irradiance;
        shared.proj_view = std::bit_cast<fmat4x4>(frame.proj_view);
        shared.camPos = std::bit_cast<fvec4>(frame.camera);
        shared.flags = std::bit_cast<fvec4>(frame.flags);
        shared.lightDir = std::bit_cast<fvec4>(frame.light_direction);
        shared.lightCol = std::bit_cast<fvec4>(frame.light_color);
        shared.envMtx = std::bit_cast<fmat4x4>(frame.env_rotation);
        shared.exposurePad = std::bit_cast<fvec4>(frame.exposure);
        directEnabled = s.do_diffuse || s.do_specular;
        iblEnabled = s.do_diffuse_ibl || s.do_specular_ibl;
        rasterizer.syncShaderConditions();
        final_output = final_output_image;
        auto batch = rasterizer.beginBatch(disney_width, disney_height);

        for (int row = 0; row < chart_row_count; ++row)
        {
            for (int col = 0; col < chart_col_count; ++col)
            {
                const auto draw = sphere_parameters(row, col);
                batch.draw(sphere, {}, draw);
            }
        }

        if (run.present_normally) {
            render_executor << std::move(batch);
            const bool presented = display.present(render_executor, final_output_image);
            bench::observe_present(run.presents, window.get(), presented);
        } else {
            bench::submit_measured(run, record_start, render_executor, std::move(batch), display, final_output_image);
        }
        bench::finish_frame(run, window.get(), chart_sphere_count, final_output_image, render_executor);
    }

    render_executor.waitForDeferredResources();
}
