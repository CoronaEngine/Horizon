#version 450

// Sponza 太阳 RSM(Reflective Shadow Map)pass,顶点阶段。
//
// 在原 depth-only 阴影 pass 的基础上升级为 MRT:除了深度,还输出世界法线与
// flux(albedo × 太阳色),给 light pass 的 RSM 单次弹射间接光 gather 用
// (Dachsbacher & Stamminger 2005;平行光用正交投影,布局同 example_rsm)。
//
// 顶点布局必须与共享的 HZMS 缓冲一致(位置/法线/切线/uv 都在),因为同一份
// 顶点缓冲也喂 G-buffer pass。

layout(set = 3, binding = 0) uniform SponzaShadowShared {
    mat4 light_view_proj;
    vec4 sun_color; // rgb: 太阳色(flux 用), w: 未用
} vsp;

// 与 sponza_shadow_frag.glsl 共享同一 push constant 块,布局必须一致。
layout(push_constant) uniform SponzaShadowPC {
    vec4 base_color_factor;
    uint tex_base_color_index;
    uint tex_mask_index;
    uint material_flags; // bit1: alpha mask, bit3: mask 采 alpha 通道(与 G-buffer pass 相同)
} model_pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec2 v_texcoord;
layout(location = 1) out vec3 v_normal;

void main()
{
    // 节点变换已烘焙进顶点,object space 即 world space。
    gl_Position = vsp.light_view_proj * vec4(inPosition, 1.0);
    v_texcoord = inTexCoord;
    v_normal = inNormal;
}
