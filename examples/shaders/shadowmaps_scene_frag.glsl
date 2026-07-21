#version 450

// 移植自参考示例 16-shadowmaps 的 fs_shadowmaps_color_lighting_hard.sc
// + common.sh（SpotLight / InvZ / Hard 默认路径）：
// view 空间 Blinn-Phong + 聚光衰减 + 硬阴影（RGBA8 解包深度）+ 指数雾。

layout(binding = 0) uniform ShadowSceneParams {
    mat4 mvp;
    mat4 model_view;
    mat4 light_mtx;
    vec4 light_pos_vs;
    vec4 light_ambient;
    vec4 light_diffuse;
    vec4 light_specular;
    vec4 spot_dir_inner_vs;
    vec4 attn_spot_outer;
    vec4 material_ka;
    vec4 material_kd;
    vec4 material_ks;
    vec4 color;
    vec4 params1;
} fsp;

layout(binding = 2) uniform sampler2D shadowMap;

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
    if (outside)
        return 1.0;

    float receiver = (shadowCoord.z - bias) / shadowCoord.w;
    float occluder = unpackRgbaToFloat(texture(shadowMap, texCoord));
    return step(receiver, occluder);
}

void main()
{
    float visibility = hardShadow(v_shadowcoord, fsp.params1.x);

    vec3 v = v_view;
    vec3 vd = -normalize(v_view);
    vec3 n = v_normal;

    // evalLight（spot：位置光 + 锥形衰减，全部在 view 空间）
    vec3 l = fsp.light_pos_vs.xyz - v;
    vec3 ld = normalize(l);
    float ldotsd = max(0.0, dot(-ld, normalize(fsp.spot_dir_inner_vs.xyz)));
    float falloff = spotFalloff(ldotsd, fsp.attn_spot_outer.w, fsp.spot_dir_inner_vs.w);
    float attn = attenuation(length(l), fsp.attn_spot_outer.xyz) *
                 mix(falloff, 1.0, step(90.0, fsp.attn_spot_outer.w));

    vec2 lc = lit(ld, n, vd, fsp.material_ks.w) * attn;

    vec3 ambi = fsp.light_ambient.xyz * fsp.light_ambient.w * fsp.material_ka.xyz;
    vec3 diff = fsp.light_diffuse.xyz * fsp.light_diffuse.w * fsp.material_kd.xyz * lc.x;
    vec3 spec = fsp.light_specular.xyz * fsp.light_specular.w * fsp.material_ks.xyz * lc.y;

    // 指数雾
    vec3 fogColor = vec3(0.0);
    float fogDensity = 0.0035;
    float LOG2 = 1.442695;
    float z = length(v);
    float fogFactor = clamp(1.0 / exp2(fogDensity * fogDensity * z * z * LOG2), 0.0, 1.0);

    vec3 baseColor = fsp.color.xyz;
    vec3 ambient = ambi * baseColor;
    vec3 brdf = (diff + spec) * baseColor * visibility;

    vec3 final = pow(abs(ambient + brdf), vec3(1.0 / 2.2)); // toGamma
    outColor = vec4(mix(fogColor, final, fogFactor), 1.0);
}
