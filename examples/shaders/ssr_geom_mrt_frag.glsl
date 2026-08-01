#version 450

// 移植自参考示例 44-sss 的 fs_sss_gbuffer.sc（SSR 版本）：G-buffer 单 pass MRT。
//
// 原版 44-sss 把「直接光」与「G-buffer」分成 gbuffer + deferred_combine 两趟，
// 这里合成一趟输出（SSR 需要一张已着色的颜色图去采样命中点，基础光照按原版
// 的 "very basic lighting" 精神就地算完）：
//   outColor    RGBA16F  rgb: 直接光结果, a: reflectivity  ← SSR 采样源
//   outNormal   RGBA8    rgb: view 空间法线, a: roughness
//   outDepthVal R32F     r  : 器件深度（下一站转线性 view Z）
//
// 三附件 MRT 与 deferred_geom_mrt_frag.glsl 相同。

layout(set = 3, binding = 0) uniform SsrGeomShared {
    mat4 view_proj;
    mat4 view;
    vec4 light_dir_vs;
} fsp;

// 与 ssr_geom_vert.glsl 共享同一 push constant 块（布局一致）。EDSL 同时继承
// vert/frag 的 ResourceBindings，两 stage 实例名必须不同以避免 C++ 端二义（C2385）。
// C++ 通过 vert 的 vpc 写入，frag 用 vpc_fs 只读。
layout(push_constant) uniform SsrGeomPC {
    mat4 model;
    vec4 material;
    vec4 params;
} vpc_fs;

layout(location = 0) in vec3 v_normal_vs;
layout(location = 1) in vec3 v_pos_vs;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outDepthVal;
layout(location = 3) out vec4 outAlbedoMet;

const float PI = 3.14159265359;

float schlick5(float u) { float m = clamp(1.0 - u, 0.0, 1.0); return m * m * m * m * m; }

void main()
{
    vec3 n = normalize(v_normal_vs);
    vec3 l = normalize(fsp.light_dir_vs.xyz);
    vec3 v = normalize(-v_pos_vs); // 相机在 view 空间原点

    vec3 albedo = vpc_fs.material.rgb;
    float metallic = vpc_fs.params.x;   // SSSR:params.x 语义改为 metallic
    float roughness = clamp(vpc_fs.params.y, 0.05, 1.0);

    // Disney 直射(diffuse*(1-F) + GGX specular),光色白光,ambient 沿用 light_dir_vs.w
    float NoL = max(dot(n, l), 0.0);
    float NoV = max(dot(n, v), 1e-4);
    vec3 h = normalize(l + v);
    float NoH = max(dot(n, h), 0.0);
    float LoH = max(dot(l, h), 0.0);

    float a = roughness * roughness;
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = f0 + (vec3(1.0) - f0) * schlick5(LoH);

    float FD90 = 0.5 + 2.0 * roughness * LoH * LoH;
    float fd = (1.0 + (FD90 - 1.0) * schlick5(NoL)) * (1.0 + (FD90 - 1.0) * schlick5(NoV));
    vec3 diffuse = albedo * (fd / PI) * (1.0 - metallic);

    float a2 = a * a;
    float dt = 1.0 + (a2 - 1.0) * NoH * NoH;
    float D = a2 / (PI * dt * dt);
    float g1l = 2.0 * NoL / (NoL + sqrt(a2 + (1.0 - a2) * NoL * NoL));
    float g1v = 2.0 * NoV / (NoV + sqrt(a2 + (1.0 - a2) * NoV * NoV));
    vec3 spec = D * F * (g1l * g1v) / max(4.0 * NoL * NoV, 1e-4);

    vec3 lit = (diffuse * (vec3(1.0) - F) + spec) * NoL + albedo * fsp.light_dir_vs.w;

    outColor = vec4(lit, 1.0); // a=1:标记「有几何」(清屏为 0,trace 据此跳过天空)
    outNormal = vec4(n * 0.5 + 0.5, roughness);
    outDepthVal = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
    outAlbedoMet = vec4(albedo, metallic); // SSSR:trace 需要 albedo/metallic 建 Disney 材质
}
