#version 450

// Sponza 太阳 RSM pass,片元阶段,三附件 MRT:
//   outDepth  R32F   器件深度(阴影比较;世界坐标由 uv+depth 经 inv_sun_view_proj 重建)
//   outNormal RGBA8  世界法线 * 0.5 + 0.5(几何法线即可,GI 是低频信号)
//   outFlux   RGBA8  albedo(sRGB 原值);光色与线性化在 gather 侧统一做,
//                    避免 8bit 缓冲里存过暗的线性值丢精度。

#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

layout(set = 3, binding = 0) uniform SponzaShadowShared {
    mat4 light_view_proj;
    vec4 sun_color;
} fsp;

layout(push_constant) uniform SponzaShadowPC {
    vec4 base_color_factor;
    uint tex_base_color_index;
    uint tex_mask_index;
    uint material_flags;
} model_pc_fs;

const uint MATERIAL_ALPHA_MASK = 2u;
const uint MATERIAL_MASK_USES_ALPHA = 8u;

#define texBaseColor combinedTextureSamplerHandles[model_pc_fs.tex_base_color_index]
#define texMask      combinedTextureSamplerHandles[model_pc_fs.tex_mask_index]

layout(location = 0) in vec2 v_texcoord;
layout(location = 1) in vec3 v_normal;

layout(location = 0) out vec4 outDepth;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outFlux;

void main()
{
    // 镂空材质在这里也要抠掉,否则投的是包围四边形的影子。
    if ((model_pc_fs.material_flags & MATERIAL_ALPHA_MASK) != 0u)
    {
        vec4 maskSample = texture(texMask, v_texcoord);
        float mask = ((model_pc_fs.material_flags & MATERIAL_MASK_USES_ALPHA) != 0u)
                         ? maskSample.a : maskSample.r;
        if (mask < 0.5)
            discard;
    }

    vec3 albedo = texture(texBaseColor, v_texcoord).rgb * model_pc_fs.base_color_factor.rgb;

    outDepth = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
    outNormal = vec4(normalize(v_normal) * 0.5 + 0.5, 1.0);
    outFlux = vec4(albedo, 1.0);
}
