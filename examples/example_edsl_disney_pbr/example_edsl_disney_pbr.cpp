#include "example_edsl_disney_pbr.h"
#include "example_disney_pbr/example_disney_pbr.h"
#include "legacy_edsl_support.h"

using namespace legacy_example::edsl;

namespace legacy_disney_edsl {

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
}
using namespace legacy_disney_edsl;

struct DisneyEdslPipeline::State {
    DisneyEdslSharedProxy shared;
    TextureCube<fvec4> texCube, texCubeIrr;
    Texture2D<fvec4> final_output;
    std::unique_ptr<RasterizerPipeline<>> pipeline;
    bool directEnabled = true;
    bool iblEnabled = true;
    State() {
        const float fov_height = std::tan(glm::radians(45.0f) * 0.5f);
        auto edsl_vertex = [&](Aggregate<DisneyEdslVertexProxy> vertex) {
            auto per_model = rasterDrawParameter<fmat4x4>(0);
            auto per_mat3 = rasterDrawParameter<fvec4>(28);

            Aggregate<DisneyEdslVaryings> out;

            $IF(per_mat3->y > Float(0.5))
            {
                // skybox：把网格顶点当全屏坐标用，方向由 skyEnvMtx 还原
                position() = Float4(vertex->pos->x, vertex->pos->y, Float(0.999999), Float(1.0));
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
                position() = mul(mul(shared.proj_view, per_model), local_pos);
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

        // x: doDiffuse, y: doSpecular, z: doDiffuseIbl, w: doSpecularIbl
        auto edsl_fragment = [&](Aggregate<DisneyEdslVaryings> in) {
            Float4 per_mat0;
            per_mat0 = rasterDrawParameter<fvec4>(16);
            Float4 per_mat1;
            per_mat1 = rasterDrawParameter<fvec4>(20);
            Float4 per_mat2;
            per_mat2 = rasterDrawParameter<fvec4>(24);
            Float4 per_mat3;
            per_mat3 = rasterDrawParameter<fvec4>(28);

            Float4 out_color;

            $IF(per_mat3->y > Float(0.5))
            {
                Float3 sky_dir;
                sky_dir = normalize(in->v_dir);
                Float bgType = per_mat3->w;
                Float3 sky_color;
                $IF(bgType >= Float(6.5))
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
                out_color = Float4(toFilmic(color), Float(1.0));
            };

            final_output << out_color;
        };
        pipeline = std::make_unique<RasterizerPipeline<>>(edsl_vertex, edsl_fragment, 1, options());
    }
};

DisneyEdslPipeline::DisneyEdslPipeline() : state_(std::make_unique<State>()) {}
DisneyEdslPipeline::~DisneyEdslPipeline() = default;
void DisneyEdslPipeline::bind_textures(HardwareImage& radiance, HardwareImage& irradiance) {
    state_->texCube = radiance;
    state_->texCubeIrr = irradiance;
}
void DisneyEdslPipeline::setDepthImage(HardwareImage& image) { state_->pipeline->setDepthImage(image); }
RasterBatch DisneyEdslPipeline::beginBatch(uint16_t width, uint16_t height) {
    state_->shared.proj_view = value(vsp.proj_view);
    state_->shared.skyEnvMtx = value(vsp.skyEnvMtx);
    state_->shared.envMtx = value(vsp.envMtx);
    state_->shared.camPos = value(vsp.camPos);
    state_->shared.flags = value(vsp.flags);
    state_->shared.lightDir = value(vsp.lightDir);
    state_->shared.lightCol = value(vsp.lightCol);
    state_->shared.exposurePad = value(vsp.exposurePad);
    state_->directEnabled = vsp.flags.x > 0.5f || vsp.flags.y > 0.5f;
    state_->iblEnabled = vsp.flags.z > 0.5f || vsp.flags.w > 0.5f;
    state_->pipeline->syncShaderConditions();
    state_->final_output = outColor;
    return state_->pipeline->beginBatch(width, height);
}
void run_example_edsl_disney_pbr() { run_example_disney_pbr(true); }
