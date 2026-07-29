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

void main()
{
    vec3 n = normalize(v_normal_vs);
    vec3 l = normalize(fsp.light_dir_vs.xyz);
    vec3 v = normalize(-v_pos_vs); // 相机在 view 空间原点

    vec3 albedo = vpc_fs.material.rgb;
    float reflectivity = vpc_fs.params.x;
    float roughness = vpc_fs.params.y;

    // Lambert + Blinn-Phong 高光（原版同为最简光照，不是本例重点）
    float ndotl = max(0.0, dot(n, l));
    vec3 h = normalize(l + v);
    float spec = pow(max(0.0, dot(n, h)), mix(128.0, 8.0, roughness));

    vec3 lit = albedo * (fsp.light_dir_vs.w + ndotl) + vec3(spec * (1.0 - roughness) * 0.35);

    outColor = vec4(lit, reflectivity);
    outNormal = vec4(n * 0.5 + 0.5, roughness);
    outDepthVal = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
