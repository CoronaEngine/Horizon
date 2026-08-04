#version 450

// RSM 场景 pass VS：间接光 gather 在世界空间做，故输出世界坐标/法线，
// 外加光空间 shadowcoord（bias 矩阵已在 CPU 侧预乘，xy/w 即 RSM 采样 uv）。

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform RsmSceneShared {
    mat4 proj_view;       // proj * view（CPU 侧预乘）
    mat4 light_proj_view; // bias * light_proj * light_view（CPU 侧预乘）
    vec4 light_pos_ws;    // xyz: 光源位置, w: 光强（直接光）
    vec4 light_dir_ws;    // xyz: 聚光方向, w: 环境光强度
    vec4 light_color;     // rgb: 光色
    vec4 spot_params;     // x: cos(inner), y: cos(outer), z: shadow bias, w: normal offset
    vec4 rsm_params;      // x: 采样半径（uv）, y: 间接光强度, z: debug 模式
    vec4 camera_pos_ws;   // xyz: 相机位置（高光用）
} vsp;

// vert/frag 共享同一 push constant 块，布局必须一致。RSM 三张图的
// bindless 索引仅 FS 使用（同 shadowmaps 的 shadowMapIndex 传法）。
layout(push_constant) uniform RsmScenePC {
    mat4 model;  // per-draw
    vec4 albedo; // rgb: 表面反照率
    uint rsmPositionIndex;
    uint rsmNormalIndex;
    uint rsmFluxIndex;
} model_pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_world_normal;
layout(location = 2) out vec4 v_shadowcoord;

void main()
{
    vec4 world_pos = model_pc.model * vec4(inPosition, 1.0);
    gl_Position = vsp.proj_view * world_pos;

    v_world_pos = world_pos.xyz;
    v_world_normal = normalize((model_pc.model * vec4(inNormal, 0.0)).xyz);

    // 世界空间沿法线偏移再投光空间，缓解 shadow acne（配合深度 bias）
    vec3 pos_offset = world_pos.xyz + v_world_normal * vsp.spot_params.w;
    v_shadowcoord = vsp.light_proj_view * vec4(pos_offset, 1.0);
}
