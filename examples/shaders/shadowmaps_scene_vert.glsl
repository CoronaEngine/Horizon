#version 450

// 移植自参考示例 16-shadowmaps 的 vs_shadowmaps_color_lighting.sc：
// 法线/视线转 view 空间，shadowcoord 沿法线偏移（normalOffset）。
// 原版的 a_normal 是 uint8 归一化，Horizon 侧加载器已解码为 float。
// VS/FS 共用同一 layout 的 uniform block（单 set=0/binding=0），实例名 vsp/fsp。

layout(binding = 0) uniform ShadowSceneParams {
    mat4 mvp;
    mat4 model_view;
    mat4 light_mtx;        // bias * light_proj * light_view * model
    vec4 light_pos_vs;     // view 空间光源位置（w=1 表示点/聚光）
    vec4 light_ambient;    // rgb, w: power
    vec4 light_diffuse;
    vec4 light_specular;
    vec4 spot_dir_inner_vs; // xyz: view 空间聚光方向, w: 内锥角(度)
    vec4 attn_spot_outer;   // xyz: 衰减(常数/线性/二次), w: 外锥角(度)
    vec4 material_ka;
    vec4 material_kd;
    vec4 material_ks;      // w: 高光指数
    vec4 color;
    vec4 params1;          // x: shadowMapBias, y: normalOffset, z: 1/shadowMapSize
} vsp;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_view;
layout(location = 2) out vec4 v_shadowcoord;

void main()
{
    gl_Position = vsp.mvp * vec4(inPosition, 1.0);

    vec3 normal = inNormal;
    v_normal = normalize((vsp.model_view * vec4(normal, 0.0)).xyz);
    v_view = (vsp.model_view * vec4(inPosition, 1.0)).xyz;

    vec3 posOffset = inPosition + normal * vsp.params1.y;
    v_shadowcoord = vsp.light_mtx * vec4(posOffset, 1.0);
}
