#version 450

// 移植自参考示例 16-shadowmaps 的 fs_shadowmaps_color_lighting_hard.sc。
// FS 不依赖 per-draw 数据，只使用共享的 vsp UBO。

#extension GL_EXT_nonuniform_qualifier : enable

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform ShadowSceneShared {
    mat4 proj_view;
    mat4 view_matrix;
    mat4 light_proj_view;
    vec4 light_pos_vs;
    vec4 light_ambient;
    vec4 light_diffuse;
    vec4 light_specular;
    vec4 spot_dir_inner_vs;
    vec4 attn_spot_outer;
    vec4 params1;
    vec4 material_ka;
    vec4 material_kd;
    vec4 material_ks;
    vec4 color;
} fsp;

// bindless combined-image-sampler 表（set 0）+ 与 shadowmaps_scene_vert.glsl 共享的 push constant。
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

// 与 shadowmaps_scene_vert.glsl 共享同一 push constant 块（布局一致）。EDSL 同时继承
// vert/frag 的 ResourceBindings，两 stage 实例名必须不同以避免 C++ 端二义（C2385）。
// C++ 通过 vert 的 model_pc 写入，frag 用 model_pc_fs 只读。
layout(push_constant) uniform ShadowScenePC {
    mat4 model;
    uint shadowMapIndex;
} model_pc_fs;

#define shadowMap combinedTextureSamplerHandles[model_pc_fs.shadowMapIndex]

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_view;
layout(location = 2) in vec4 v_shadowcoord;

layout(location = 0) out vec4 outColor;

float unpackRgbaToFloat(vec4 rgba)
{
    const vec4 shift = vec4(1.0 / (256.0 * 256.0 * 256.0), 1.0 / (256.0 * 256.0), 1.0 / 256.0, 1.0);
    return dot(rgba, shift);
}

float attenuation(float dist, vec3 attn)
{
    return 1.0 / (attn.x + attn.y * dist + attn.z * dist * dist);
}

float spotFalloff(float ldotsd, float innerDeg, float outerDeg)
{
    float inner = cos(radians(innerDeg));
    float outer = cos(radians(min(outerDeg, innerDeg - 0.001)));
    return clamp((ldotsd - inner) / (outer - inner), 0.0, 1.0);
}

vec2 lit(vec3 ld, vec3 n, vec3 vd, float exp_)
{
    float ndotl = dot(n, ld);
    vec3 r = 2.0 * ndotl * n - ld;
    float rdotv = dot(r, vd);
    float spec = step(0.0, ndotl) * pow(max(0.0, rdotv), exp_) * (2.0 + exp_) / 8.0;
    return max(vec2(ndotl, spec), 0.0);
}

float hardShadow(vec4 shadowCoord, float bias)
{
    vec2 texCoord = shadowCoord.xy / shadowCoord.w;
    bool outside = any(greaterThan(texCoord, vec2(1.0))) || any(lessThan(texCoord, vec2(0.0)));
    if (outside) return 1.0;
    float receiver = (shadowCoord.z - bias) / shadowCoord.w;
    float occluder = unpackRgbaToFloat(texture(shadowMap, texCoord));
    return step(receiver, occluder);
}

void main()
{
    float visibility = hardShadow(v_shadowcoord, fsp.params1.x);

    vec3 v  = v_view;
    vec3 vd = -normalize(v_view);
    vec3 n  = v_normal;

    vec3 l  = fsp.light_pos_vs.xyz - v;
    vec3 ld = normalize(l);
    float ldotsd = max(0.0, dot(-ld, normalize(fsp.spot_dir_inner_vs.xyz)));
    float falloff = spotFalloff(ldotsd, fsp.attn_spot_outer.w, fsp.spot_dir_inner_vs.w);
    float attn = attenuation(length(l), fsp.attn_spot_outer.xyz) *
                 mix(falloff, 1.0, step(90.0, fsp.attn_spot_outer.w));

    vec2 lc = lit(ld, n, vd, fsp.material_ks.w) * attn;

    vec3 ambi = fsp.light_ambient.xyz  * fsp.light_ambient.w  * fsp.material_ka.xyz;
    vec3 diff = fsp.light_diffuse.xyz  * fsp.light_diffuse.w  * fsp.material_kd.xyz * lc.x;
    vec3 spec = fsp.light_specular.xyz * fsp.light_specular.w * fsp.material_ks.xyz * lc.y;

    vec3 fogColor   = vec3(0.0);
    float fogDensity = 0.0035;
    float LOG2       = 1.442695;
    float z          = length(v);
    float fogFactor  = clamp(1.0 / exp2(fogDensity * fogDensity * z * z * LOG2), 0.0, 1.0);

    vec3 baseColor = fsp.color.xyz;
    vec3 ambient   = ambi * baseColor;
    vec3 brdf      = (diff + spec) * baseColor * visibility;

    vec3 final = pow(abs(ambient + brdf), vec3(1.0 / 2.2));
    outColor = vec4(mix(fogColor, final, fogFactor), 1.0);
}
