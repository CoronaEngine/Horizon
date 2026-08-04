#version 450

// 移植自参考示例 44-sss 的 vs_sss_gbuffer.sc（SSR 版本）。
//
// SSR 的步进在 view 空间进行（同 44-sss 的 fs_screen_space_shadows.sc），
// 因此 G-buffer 输出 view 空间法线，并把 view 空间坐标传给 FS 做基础光照。
//
// 拆分策略（同 deferred_geom_vert.glsl）：
//   - UBO vsp：view_proj + view + light_dir_vs + 完整 Disney 参数（批次共享）
//   - PC  vpc：model + material.rgb（per-draw）96→80 bytes

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform SsrGeomShared {
    mat4 view_proj;
    mat4 view;
    vec4 light_dir_vs;  // xyz: view 空间指向光源的方向, w: 环境光强度
    // Disney Principled BRDF 参数（批次共享，所有物体相同；仅 albedo 随 per-draw 变化）
    vec4 disney_a;      // x=metallic, y=roughness, z=specular, w=specular_tint
    vec4 disney_b;      // x=subsurface, y=anisotropic, z=sheen, w=sheen_tint
    vec4 disney_c;      // x=clearcoat, y=clearcoat_gloss
} vsp;

// vert/frag 共享同一 push constant 块，布局必须一致。
// params（原 metallic/roughness）已移入 vsp，PC 仅保留几何变换与 albedo。
layout(push_constant) uniform SsrGeomPC {
    mat4 model;     // per-draw 变换
    vec4 material;  // rgb: albedo, w: 未用
} vpc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 v_normal_vs;
layout(location = 1) out vec3 v_pos_vs;

void main()
{
    mat4 model_view = vsp.view * vpc.model;

    gl_Position = vsp.view_proj * vpc.model * vec4(inPosition, 1.0);

    v_pos_vs    = (model_view * vec4(inPosition, 1.0)).xyz;
    v_normal_vs = normalize((model_view * vec4(inNormal, 0.0)).xyz);
}
