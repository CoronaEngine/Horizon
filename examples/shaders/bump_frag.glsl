#version 450

// 移植自参考示例 06-bump 的 fs_bump.sc：法线贴图 + 4 点光源（切线空间光照）。
// FS 只需要共享的 vsp（UBO），不依赖 per-draw 的 pc.model。

#extension GL_EXT_nonuniform_qualifier : enable

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
} fsp;

// bindless combined-image-sampler 表（set 0）+ 与 bump_vert.glsl 共享的 push constant。
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

// 与 bump_vert.glsl 共享同一 push constant 块（布局一致）。EDSL 将 vert/frag 的
// ResourceBindings 一并继承，故两个 stage 的 push constant 实例名必须不同，否则 C++
// 端成员访问二义（C2385）。C++ 通过 vert 的 model_pc 写入，frag 用 model_pc_fs 只读。
layout(push_constant) uniform BumpPC {
    mat4 model;
    uint texColorIndex;
    uint texNormalIndex;
} model_pc_fs;

#define texColor  combinedTextureSamplerHandles[model_pc_fs.texColorIndex]
#define texNormal combinedTextureSamplerHandles[model_pc_fs.texNormalIndex]

layout(location = 0) in vec3 v_wpos;
layout(location = 1) in vec3 v_view;
layout(location = 2) in vec3 v_normal;
layout(location = 3) in vec3 v_tangent;
layout(location = 4) in vec3 v_bitangent;
layout(location = 5) in vec2 v_texcoord;

layout(location = 0) out vec4 outColor;

vec2 blinn(vec3 lightDir, vec3 normal, vec3 viewDir)
{
    float ndotl = dot(normal, lightDir);
    vec3 reflected = 2.0 * ndotl * normal - lightDir;
    float rdotv = dot(reflected, viewDir);
    return vec2(ndotl, rdotv);
}

vec4 lit(float ndotl, float rdotv, float m)
{
    float diff = max(0.0, ndotl);
    float spec = step(0.0, ndotl) * max(0.0, rdotv * m);
    return vec4(1.0, diff, spec, 1.0);
}

vec3 calcLight(vec4 lightPosRadius, vec4 lightRgbInnerR, mat3 tbn, vec3 wpos, vec3 normal, vec3 view)
{
    vec3 lp = lightPosRadius.xyz - wpos;
    float attn = 1.0 - smoothstep(lightRgbInnerR.w, 1.0, length(lp) / lightPosRadius.w);
    vec3 lightDir = normalize(lp) * tbn;
    vec2 bln = blinn(lightDir, normal, view);
    vec4 lc = lit(bln.x, bln.y, 1.0);
    return lightRgbInnerR.xyz * clamp(lc.y, 0.0, 1.0) * attn;
}

void main()
{
    mat3 tbn = mat3(v_tangent, v_bitangent, v_normal);

    vec3 normal;
    normal.xy = texture(texNormal, v_texcoord).xy * 2.0 - 1.0;
    normal.z  = sqrt(max(0.0, 1.0 - dot(normal.xy, normal.xy)));
    vec3 view = normalize(v_view);

    vec3 lightColor;
    lightColor  = calcLight(fsp.light0_pos_radius, fsp.light0_rgb_inner_r, tbn, v_wpos, normal, view);
    lightColor += calcLight(fsp.light1_pos_radius, fsp.light1_rgb_inner_r, tbn, v_wpos, normal, view);
    lightColor += calcLight(fsp.light2_pos_radius, fsp.light2_rgb_inner_r, tbn, v_wpos, normal, view);
    lightColor += calcLight(fsp.light3_pos_radius, fsp.light3_rgb_inner_r, tbn, v_wpos, normal, view);

    vec4 color = vec4(pow(texture(texColor, v_texcoord).xyz, vec3(2.2)), 1.0); // toLinear

    vec3 rgb = max(vec3(0.05), lightColor) * color.xyz;
    outColor = vec4(rgb, 1.0);
}
