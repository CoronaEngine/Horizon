#version 450

// 移植自参考示例 16-shadowmaps 的 vs_shadowmaps_color_lighting.sc。
//
// 拆分策略：
//   - UBO  vsp：批次共享（相机矩阵、光源参数、材质，全场景相同）304 bytes
//   - PC   pc ：per-draw（model 矩阵，64 bytes）
// mvp / model_view / light_mtx 在 VS 内从 vsp.proj_view * pc.model 等现场计算。

layout(binding = 0) uniform ShadowSceneShared {
    mat4 proj_view;       // proj * view（CPU 侧预乘）
    mat4 view_matrix;     // view 矩阵（用于法线/视线变换）
    mat4 light_proj_view; // bias * light_proj * light_view（CPU 侧预乘）
    vec4 light_pos_vs;
    vec4 light_ambient;
    vec4 light_diffuse;
    vec4 light_specular;
    vec4 spot_dir_inner_vs;
    vec4 attn_spot_outer;
    vec4 params1;         // x: shadowMapBias, y: normalOffset, z: 1/shadowMapSize
    vec4 material_ka;
    vec4 material_kd;
    vec4 material_ks;     // w: 高光指数
    vec4 color;
} vsp;

layout(push_constant) uniform ShadowScenePC {
    mat4 model; // per-draw
} model_pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_view;
layout(location = 2) out vec4 v_shadowcoord;

void main()
{
    // 从共享矩阵 + per-draw model 现场计算各变换
    mat4 mvp        = vsp.proj_view       * model_pc.model;
    mat4 model_view = vsp.view_matrix     * model_pc.model;
    mat4 light_mtx  = vsp.light_proj_view * model_pc.model;

    gl_Position = mvp * vec4(inPosition, 1.0);

    vec3 normal = inNormal;
    v_normal = normalize((model_view * vec4(normal, 0.0)).xyz);
    v_view   = (model_view * vec4(inPosition, 1.0)).xyz;

    vec3 posOffset = inPosition + normal * vsp.params1.y;
    v_shadowcoord  = light_mtx * vec4(posOffset, 1.0);
}
