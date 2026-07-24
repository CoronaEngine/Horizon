#version 450

// 移植自参考示例 06-bump 的 vs_bump.sc（非实例化路径）：
// 世界空间 TBN，view 向量转到切线空间。
//
// 拆分策略：
//   - UBO  vsp：批次内共享（view_proj、eye_pos、4 组光源参数）208 bytes
//   - PC   pc ：per-draw（model 矩阵，每个 cube 不同）          64 bytes

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform BumpShared {
    mat4 view_proj;
    vec4 eye_pos;
    vec4 light0_pos_radius;
    vec4 light1_pos_radius;
    vec4 light2_pos_radius;
    vec4 light3_pos_radius;
    vec4 light0_rgb_inner_r;
    vec4 light1_rgb_inner_r;
    vec4 light2_rgb_inner_r;
    vec4 light3_rgb_inner_r;
} vsp;

// vert/frag 共享同一 push constant 块，布局必须一致。
// texColorIndex/texNormalIndex 为 bindless combined-texture 表（set 0）索引，仅 FS 使用。
layout(push_constant) uniform BumpPC {
    mat4 model;
    uint texColorIndex;
    uint texNormalIndex;
} model_pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent; // w: bitangent 符号
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 v_wpos;
layout(location = 1) out vec3 v_view;
layout(location = 2) out vec3 v_normal;
layout(location = 3) out vec3 v_tangent;
layout(location = 4) out vec3 v_bitangent;
layout(location = 5) out vec2 v_texcoord;

void main()
{
    vec3 wpos = (model_pc.model * vec4(inPosition, 1.0)).xyz;
    v_wpos = wpos;

    gl_Position = vsp.view_proj * vec4(wpos, 1.0);

    vec3 wnormal  = normalize((model_pc.model * vec4(inNormal,       0.0)).xyz);
    vec3 wtangent = normalize((model_pc.model * vec4(inTangent.xyz,  0.0)).xyz);

    v_normal    = wnormal;
    v_tangent   = wtangent;
    v_bitangent = cross(v_normal, v_tangent) * inTangent.w;

    mat3 tbn = mat3(v_tangent, v_bitangent, v_normal);

    // 切线空间 view 向量
    v_view    = (vsp.eye_pos.xyz - wpos) * tbn;
    v_texcoord = inTexCoord;
}
