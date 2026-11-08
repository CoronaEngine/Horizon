#include "example_edsl_ibl_old.h"
#include "example_ibl_old/example_ibl_old.h"
#include "legacy_edsl_support.h"

using namespace legacy_example::edsl;

namespace legacy_ibl_edsl {
struct IblEdslVertexProxy
{
    EmbeddedShader::Float3 pos;
    EmbeddedShader::Float3 normal;
};

struct IblEdslVaryings
{
    EmbeddedShader::Float3 v_view;
    EmbeddedShader::Float3 v_normal;
    EmbeddedShader::Float3 v_dir;
};

struct IblEdslSharedProxy
{
    EmbeddedShader::Float4x4 proj_view;  // 替换 mvp：由 VS 现场计算 proj_view * per_model
    EmbeddedShader::Float4x4 skyEnvMtx;
    EmbeddedShader::Float4x4 envMtx;
    EmbeddedShader::Float4   camPos;
    EmbeddedShader::Float4   flags;     // x:doDiffuse, y:doSpecular, z:doDiffuseIbl, w:doSpecularIbl
    EmbeddedShader::Float4   rgbDiff;
    EmbeddedShader::Float4   rgbSpec;
    EmbeddedShader::Float4   lightDir;
    EmbeddedShader::Float4   lightCol;
    // 3×64 + 6×16 = 288 bytes → UBO（批次共享）
};
}
using namespace legacy_ibl_edsl;

struct IblEdslOldPipeline::State {
    IblEdslSharedProxy shared;
    Float4x4 per_model;
    Float4 per_params0;
    Float4 per_misc;
    Texture2D<fvec4> texCube, texCubeIrr, final_output;
    std::unique_ptr<RasterizerPipeline<>> pipeline;
    State() {
        per_model.as_push_constant();
        per_params0.as_push_constant();
        per_misc.as_push_constant();
        auto edsl_ibl_vertex = [&](
            Aggregate<IblEdslVertexProxy> vertex
        ) {
            Aggregate<IblEdslVaryings> out;

            Float isSkybox = per_misc->x;
            $IF (isSkybox > Float(0.5))
            {
                // 天空盒：NDC 全屏三角形，钉在远平面 z=1
                position() = Float4(vertex->pos->x, vertex->pos->y, Float(0.999999), Float(1.0));

                Float fovHeight = tan(radians(Float(45.0)) * Float(0.5));
                Float aspect    = per_misc->y;
                Float2 tex      = Float2(vertex->pos->x, vertex->pos->y)
                                * Float2(fovHeight * aspect, -fovHeight);

                out->v_dir    = mul(shared.skyEnvMtx, Float4(tex->x, tex->y, Float(1.0), Float(0.0)))->xyz();
                out->v_view   = Float3(Float(0.0), Float(0.0), Float(0.0));
                out->v_normal = Float3(Float(0.0), Float(0.0), Float(1.0));
            }
            $ELSE
            {
                // 网格路径：mvp = proj_view * per_model（逐层 mul 等价于矩阵乘法结合律）
                Float4 local_pos  = Float4(vertex->pos, Float(1.0));
                Float4 world_pos  = mul(per_model, local_pos);
                position() = mul(mul(shared.proj_view, per_model), local_pos);

                out->v_view   = shared.camPos->xyz() - world_pos->xyz();
                out->v_normal = mul(per_model, Float4(vertex->normal, Float(0.0)))->xyz();
                out->v_dir    = Float3(Float(0.0), Float(0.0), Float(0.0));
            };
            return out;
        };

        // 片元辅助函数
        auto toLinear = [](Float3 rgb) -> Float3 {
            return pow(abs(rgb), Float3(Float(2.2), Float(2.2), Float(2.2)));
        };

        auto toFilmic = [](Float3 rgb) -> Float3 {
            rgb = max(Float3(Float(0.0), Float(0.0), Float(0.0)), rgb - Float3(Float(0.004), Float(0.004), Float(0.004)));
            return (rgb * (Float(6.2) * rgb + Float3(Float(0.5), Float(0.5), Float(0.5))))
                 / (rgb * (Float(6.2) * rgb + Float3(Float(1.7), Float(1.7), Float(1.7))) + Float3(Float(0.06), Float(0.06), Float(0.06)));
        };

        auto fixCubeLookup = [](Float3 v, Float lod, Float topLevelCubeSize) -> Float3 {
            Float ax = abs(v->x);
            Float ay = abs(v->y);
            Float az = abs(v->z);
            Float vmax = max(max(ax, ay), az);
            Float scale = Float(1.0) - exp2(lod) / topLevelCubeSize;
            $IF (ax != vmax) { v->x = v->x * scale; };
            $IF (ay != vmax) { v->y = v->y * scale; };
            $IF (az != vmax) { v->z = v->z * scale; };
            return v;
        };

        auto calcFresnel = [](Float3 cspec, Float dotNV, Float strength) -> Float3 {
            return cspec + (Float3(1.0, 1.0, 1.0) - cspec)
                         * pow(Float(1.0) - dotNV, Float(5.0)) * strength;
        };

        auto calcLambert = [](Float3 cdiff, Float ndotl) -> Float3 {
            return cdiff * ndotl;
        };

        auto calcBlinn = [](Float3 cspec, Float ndoth, Float ndotl, Float specPower) -> Float3 {
            Float norm = (specPower + Float(8.0)) * Float(0.125);
            Float brdf = pow(ndoth, specPower) * ndotl * norm;
            return cspec * brdf;
        };

        auto specPwr = [](Float gloss) -> Float {
            return exp2(Float(10.0) * gloss + Float(2.0));
        };


        auto edsl_ibl_fragment = [&](
            Aggregate<IblEdslVaryings> in
        ) {
            Float isSkybox = per_misc->x;
            Float4 finalColor;
            $IF(isSkybox > Float(0.5))
            {
                // 天空盒
                Float3 dir = normalize(in->v_dir);
                Float bgType = per_params0->w;
                Float3 color;

                $IF(bgType == Float(7.0))
                {
                    color = toLinear(sample_cube(texCubeIrr, dir, Float(0))->xyz());
                }
                $ELSE
                {
                    Float lod = bgType;
                    Float3 nDir = fixCubeLookup(dir, lod, Float(256.0));
                    color = toLinear(sample_cube(texCube, nDir, lod)->xyz());
                };

                color = color * exp2(per_params0->z);
                finalColor = Float4(toFilmic(color), Float(1.0));
            }
            $ELSE
            {
                // IBL 网格着色
                Float3 ld     = normalize(Float3(shared.lightDir->xyz()));
                Float3 clight = shared.lightCol->xyz();

                Float3 nn = normalize(in->v_normal);
                Float3 vv = normalize(in->v_view);
                Float3 hh = normalize(vv + ld);

                Float ndotv = clamp(dot(nn, vv), Float(0.0), Float(1.0));
                Float ndotl = clamp(dot(nn, ld), Float(0.0), Float(1.0));
                Float ndoth = clamp(dot(nn, hh), Float(0.0), Float(1.0));
                Float hdotv = clamp(dot(hh, vv), Float(0.0), Float(1.0));

                Float3 inAlbedo       = shared.rgbDiff->xyz();
                Float  inReflectivity = per_params0->y;
                Float  inGloss        = per_params0->x;

                Float3 refl;
                $IF(per_misc->z == Float(0.0))
                {
                    refl = mix(Float3(Float(0.04), Float(0.04), Float(0.04)),
                               inAlbedo, inReflectivity);
                }
                $ELSE
                {
                    refl = shared.rgbSpec->xyz() * Float3(inReflectivity, inReflectivity, inReflectivity);
                };

                Float3 albedo      = inAlbedo * (Float3(Float(1.0), Float(1.0), Float(1.0)) - inReflectivity);
                Float3 dirFresnel  = calcFresnel(refl, hdotv, inGloss);
                Float3 envFresnel  = calcFresnel(refl, ndotv, inGloss);

                Float3 lambert = shared.flags->x * calcLambert(albedo * (Float3(Float(1.0), Float(1.0), Float(1.0)) - dirFresnel), ndotl);
                Float3 blinn   = shared.flags->y * calcBlinn(dirFresnel, ndoth, ndotl, specPwr(inGloss));
                Float3 direct  = (lambert + blinn) * clight;

                Float mip = Float(1.0) + Float(5.0) * (Float(1.0) - inGloss);

                Float3 vr = Float(2.0) * ndotv * nn - vv;
                Float3 cubeR = normalize(Float3(mul(shared.envMtx, Float4(vr, Float(0.0)))->xyz()));
                Float3 cubeN = normalize(Float3(mul(shared.envMtx, Float4(nn, Float(0.0)))->xyz()));
                Float3 nCubeR = fixCubeLookup(cubeR, mip, Float(256.0));

                Float3 radiance    = toLinear(sample_cube(texCube, nCubeR, mip)->xyz());
                Float3 irradiance  = toLinear(sample_cube(texCubeIrr, cubeN, Float(0))->xyz());
                Float3 envDiffuse  = albedo     * irradiance * shared.flags->z;
                Float3 envSpecular = envFresnel * radiance   * shared.flags->w;
                Float3 indirect    = envDiffuse + envSpecular;

                Float3 color = direct + indirect;
                Float3 nColor = color * exp2(per_params0->z);
                finalColor = Float4(toFilmic(nColor), Float(1.0));
            }
            final_output << finalColor;
        };
        pipeline = std::make_unique<RasterizerPipeline<>>(edsl_ibl_vertex, edsl_ibl_fragment, 1, options());
    }
};

IblEdslOldPipeline::IblEdslOldPipeline() : state_(std::make_unique<State>()) {}
IblEdslOldPipeline::~IblEdslOldPipeline() = default;
void IblEdslOldPipeline::bind_textures(HardwareImage& radiance, HardwareImage& irradiance) {
    state_->texCube = radiance;
    state_->texCubeIrr = irradiance;
}
void IblEdslOldPipeline::setDepthImage(HardwareImage& image) { state_->pipeline->setDepthImage(image); }
void IblEdslOldPipeline::prepare_draws() {
    state_->shared.proj_view = value(vsp.proj_view);
    state_->shared.skyEnvMtx = value(vsp.skyEnvMtx);
    state_->shared.envMtx = value(vsp.envMtx);
    state_->shared.camPos = value(vsp.camPos);
    state_->shared.flags = value(vsp.flags);
    state_->shared.rgbDiff = value(vsp.rgbDiff);
    state_->shared.rgbSpec = value(vsp.rgbSpec);
    state_->shared.lightDir = value(vsp.lightDir);
    state_->shared.lightCol = value(vsp.lightCol);
    state_->final_output = outColor;
}
void IblEdslOldPipeline::record(const HardwareBuffer& ib, const HardwareBuffer& vb, const DrawIndexedParams& params) {
    state_->per_model = value(vpc.model);
    state_->per_params0 = value(vpc.params0);
    state_->per_misc = value(vpc.misc);
    // Legacy auto-binding happens in operator(), before record snapshots the PC.
    (*state_->pipeline)(0, 0).record(ib, vb, params);
}
RasterizerPipelineBase& IblEdslOldPipeline::operator()(uint16_t width, uint16_t height) {
    return (*state_->pipeline)(width, height);
}
void run_example_edsl_ibl_old() { run_example_ibl_old(true); }
