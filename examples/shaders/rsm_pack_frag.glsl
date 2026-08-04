#version 450

// RSM 生成 pass FS，三附件 MRT（与 ssr_geom_mrt_frag 相同的 MRT 写法）：
//   outPosition RGBA32F  xyz: 世界坐标, w: 光空间深度 z/w（硬阴影比较用）
//   outNormal   RGBA16F  xyz: 世界法线
//   outFlux     RGBA16F  rgb: albedo * 光色 * 聚光锥衰减
// flux 按论文不含距离衰减：它表示该像素光携带的辐射通量。

layout(set = 3, binding = 0) uniform RsmPackShared {
    mat4 light_view_proj;
    vec4 light_pos_ws;
    vec4 light_dir_ws;
    vec4 light_color;
    vec4 spot_params;
} fsp;

// 与 rsm_pack_vert.glsl 共享同一 push constant 块（布局一致）。EDSL 同时继承
// vert/frag 的 ResourceBindings，两 stage 实例名必须不同以避免 C++ 端二义（C2385）。
layout(push_constant) uniform RsmPackPC {
    mat4 model;
    vec4 albedo;
} pack_pc_fs;

layout(location = 0) in vec4 v_position;
layout(location = 1) in vec3 v_world_pos;
layout(location = 2) in vec3 v_world_normal;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outFlux;

void main()
{
    vec3 to_frag = normalize(v_world_pos - fsp.light_pos_ws.xyz);
    float cosang = dot(to_frag, fsp.light_dir_ws.xyz);
    float falloff = smoothstep(fsp.spot_params.y, fsp.spot_params.x, cosang);

    float depth = v_position.z / v_position.w;

    outPosition = vec4(v_world_pos, depth);
    outNormal   = vec4(normalize(v_world_normal), 0.0);
    outFlux     = vec4(pack_pc_fs.albedo.rgb * fsp.light_color.rgb * falloff, 1.0);
}
