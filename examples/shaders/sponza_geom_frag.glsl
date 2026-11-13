#version 450
#include "raster_batch.slang"
layout(location = 15) flat in uint drawParameterWord;

// Sponza G-buffer geometry pass, fragment stage: single-pass MRT writing
// albedo / world normal / device depth (PBR metallic-roughness 版本)。
//
// 两个空闲 alpha 通道打包表面参数,不需要第四个渲染目标:
//   albedo.a -> metallic  (glTF metallicRoughness 贴图 B 通道 × metallicFactor)
//   normal.a -> roughness (G 通道 × roughnessFactor)
//
// flat 材质下标来自顶点阶段的 gl_InstanceIndex，用于查找每个子网格的材质。

#extension GL_EXT_nonuniform_qualifier : enable

// bindless combined-image-sampler table (set 0).
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

// Shared with sponza_geom_vert.glsl; the layout must stay identical.
layout(set = 3, binding = 0) uniform SponzaGeomShared {
    mat4 view_proj;
} fsp;

// Draw parameter layout shared with sponza_geom_vert.glsl.
struct SponzaGeomPC {
    uint per_draw_ssbo_index;
};
#define model_pc_fs (*(SponzaGeomPC*)(drawRoot.parameters + drawParameterWord))

// 逐-draw 材质。std430 下 vec4 对齐 16,尾部显式补齐到 64 字节,
// 让 C++ 侧的 PerDrawMaterial 能按同样步长打包(布局必须逐字段一致)。
struct PerDrawMaterial {
    vec4 base_color_factor;
    vec4 mr_factor; // x: metallicFactor, y: roughnessFactor, zw: unused
    uint tex_base_color_index;
    uint tex_normal_index;
    uint tex_metal_rough_index;
    uint tex_mask_index;
    uint material_flags; // bit0: normal map, bit1: alpha mask, bit2: MR map, bit3: mask 采 alpha
    uint pad0;
    uint pad1;
    uint pad2;
};

layout(set = 1, binding = 0) readonly buffer PerDrawSSBO {
    PerDrawMaterial materials[];
} per_draw_data[];

const uint MATERIAL_HAS_NORMAL = 1u;
const uint MATERIAL_ALPHA_MASK = 2u;
const uint MATERIAL_HAS_METALROUGH = 4u;
const uint MATERIAL_MASK_USES_ALPHA = 8u;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_tangent;
layout(location = 2) in vec3 v_bitangent;
layout(location = 3) in vec2 v_texcoord;
layout(location = 4) flat in int v_draw_index;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outDepthVal;

void main()
{
    PerDrawMaterial mat =
        per_draw_data[model_pc_fs.per_draw_ssbo_index].materials[uint(v_draw_index)];

    if ((mat.material_flags & MATERIAL_ALPHA_MASK) != 0u)
    {
        vec4 maskSample = texture(combinedTextureSamplerHandles[mat.tex_mask_index], v_texcoord);
        // glTF MASK 模式的镂空存在 baseColor.a;旧资产是独立灰度 mask 图(.r)。
        float mask = ((mat.material_flags & MATERIAL_MASK_USES_ALPHA) != 0u)
                         ? maskSample.a : maskSample.r;
        if (mask < 0.5)
            discard;
    }

    vec4 base = texture(combinedTextureSamplerHandles[mat.tex_base_color_index], v_texcoord)
                * mat.base_color_factor;

    // glTF 约定:metallicRoughness 贴图 G = roughness, B = metallic,
    // 分别乘以材质常量因子;没有贴图时退化为常量因子本身。
    float metallic = mat.mr_factor.x;
    float roughness = mat.mr_factor.y;
    if ((mat.material_flags & MATERIAL_HAS_METALROUGH) != 0u)
    {
        vec4 mr = texture(combinedTextureSamplerHandles[mat.tex_metal_rough_index], v_texcoord);
        roughness *= mr.g;
        metallic *= mr.b;
    }

    vec3 n = normalize(v_normal);
    if ((mat.material_flags & MATERIAL_HAS_NORMAL) != 0u)
    {
        vec3 tangent_normal =
            texture(combinedTextureSamplerHandles[mat.tex_normal_index], v_texcoord).xyz * 2.0 - 1.0;
        mat3 tbn = mat3(normalize(v_tangent), normalize(v_bitangent), n);
        n = normalize(tbn * tangent_normal);
    }

    outAlbedo = vec4(base.rgb, metallic);
    outNormal = vec4(n * 0.5 + 0.5, roughness); // encodeNormalUint
    outDepthVal = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
