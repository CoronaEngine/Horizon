#version 450
#include "raster_batch.slang"
layout(location = 15) flat in uint drawParameterWord;

// Sponza 太阳 RSM pass,片元阶段,三附件 MRT:
//   outDepth  R32F   器件深度(阴影比较;世界坐标由 uv+depth 经 inv_sun_view_proj 重建)
//   outNormal RGBA8  世界法线 * 0.5 + 0.5(几何法线即可,GI 是低频信号)
//   outFlux   RGBA8  albedo(sRGB 原值);光色与线性化在 gather 侧统一做,
//                    避免 8bit 缓冲里存过暗的线性值丢精度。
//
// 材质来自 bindless SSBO,由顶点阶段传下的 draw 序号索引(见 sponza_shadow_vert.glsl)。

#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

layout(set = 3, binding = 0) uniform SponzaShadowShared {
    mat4 light_view_proj;
    vec4 sun_color;
} fsp;

struct SponzaShadowPC {
    uint per_draw_ssbo_index;
};
#define model_pc_fs (*(SponzaShadowPC*)(drawRoot.parameters + drawParameterWord))

// 逐-draw 材质(RSM 只需 albedo 与镂空);尾部补齐到 32 字节,
// C++ 侧 PerDrawShadowMaterial 必须逐字段一致。
struct PerDrawShadowMaterial {
    vec4 base_color_factor;
    uint tex_base_color_index;
    uint tex_mask_index;
    uint material_flags; // bit1: alpha mask, bit3: mask 采 alpha 通道(与 G-buffer pass 相同)
    uint pad0;
};

layout(set = 1, binding = 0) readonly buffer PerDrawShadowSSBO {
    PerDrawShadowMaterial materials[];
} per_draw_data[];

const uint MATERIAL_ALPHA_MASK = 2u;
const uint MATERIAL_MASK_USES_ALPHA = 8u;

layout(location = 0) in vec2 v_texcoord;
layout(location = 1) in vec3 v_normal;
layout(location = 2) flat in int v_draw_index;

layout(location = 0) out vec4 outDepth;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outFlux;

void main()
{
    PerDrawShadowMaterial mat =
        per_draw_data[model_pc_fs.per_draw_ssbo_index].materials[uint(v_draw_index)];

    // 镂空材质在这里也要抠掉,否则投的是包围四边形的影子。
    if ((mat.material_flags & MATERIAL_ALPHA_MASK) != 0u)
    {
        vec4 maskSample = texture(combinedTextureSamplerHandles[mat.tex_mask_index], v_texcoord);
        float mask = ((mat.material_flags & MATERIAL_MASK_USES_ALPHA) != 0u)
                         ? maskSample.a : maskSample.r;
        if (mask < 0.5)
            discard;
    }

    vec3 albedo = texture(combinedTextureSamplerHandles[mat.tex_base_color_index], v_texcoord).rgb
                  * mat.base_color_factor.rgb;

    outDepth = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
    outNormal = vec4(normalize(v_normal) * 0.5 + 0.5, 1.0);
    outFlux = vec4(albedo, 1.0);
}
