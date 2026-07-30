#version 450

// RSM（Reflective Shadow Maps，Dachsbacher & Stamminger 2005）生成 pass VS：
// 光源视角渲染场景，把世界坐标/法线传给 FS 写 MRT。
// 结构沿用 shadowmaps_pack_vert（光源视角 pass），额外输出世界空间量。

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform RsmPackShared {
    mat4 light_view_proj; // light_proj * light_view
    vec4 light_pos_ws;    // xyz: 光源位置
    vec4 light_dir_ws;    // xyz: 聚光方向（已归一化）
    vec4 light_color;     // rgb: 光色
    vec4 spot_params;     // x: cos(inner), y: cos(outer)
} vsp;

// vert/frag 共享同一 push constant 块，布局必须一致。
layout(push_constant) uniform RsmPackPC {
    mat4 model;  // per-draw
    vec4 albedo; // rgb: 表面反照率（flux 用）
} pack_pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec4 v_position;     // 光空间裁剪坐标（FS 取深度）
layout(location = 1) out vec3 v_world_pos;
layout(location = 2) out vec3 v_world_normal;

void main()
{
    vec4 world_pos = pack_pc.model * vec4(inPosition, 1.0);
    gl_Position = vsp.light_view_proj * world_pos;
    v_position = gl_Position;
    v_world_pos = world_pos.xyz;
    // 场景无非均匀缩放（墙/地面用预缩放顶点数据），model 直接变换法线即可
    v_world_normal = normalize((pack_pc.model * vec4(inNormal, 0.0)).xyz);
}
