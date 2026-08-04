#version 450

// 移植自参考示例 21-deferred 的 fs_deferred_light.sc。
// uniform block 与 deferred_light_vert.glsl 完全一致（共用 binding=0）。

#extension GL_EXT_nonuniform_qualifier : enable

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform DeferredLightShared {
    mat4 inv_mvp;
    mat4 view;
} fsp;

// 与 deferred_light_vert.glsl 共享同一 push constant 块，布局必须一致。
layout(push_constant) uniform DeferredLightPC {
    vec4 light_pos_radius;
    vec4 light_rgb_inner_r;
    vec4 rect;
    uint gNormalIndex;
    uint gDepthIndex;
} fpc;

// bindless combined-image-sampler 表（set 0）。
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

#define gNormal combinedTextureSamplerHandles[fpc.gNormalIndex]
#define gDepth  combinedTextureSamplerHandles[fpc.gDepthIndex]

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 outColor;

vec2 blinn(vec3 lightDir, vec3 normal, vec3 viewDir)
{
    float ndotl = dot(normal, lightDir);
    vec3 reflected = lightDir - 2.0 * ndotl * normal;
    float rdotv = dot(reflected, viewDir);
    return vec2(ndotl, rdotv);
}

vec4 lit(float ndotl, float rdotv, float m)
{
    float diff = max(0.0, ndotl);
    float spec = step(0.0, ndotl) * max(0.0, rdotv * m);
    return vec4(1.0, diff, spec, 1.0);
}

vec3 calcLight(vec3 wpos, vec3 normal, vec3 view,
               vec3 lightPos, float lightRadius, vec3 lightRgb, float lightInner)
{
    vec3 lp = lightPos - wpos;
    float attn = 1.0 - smoothstep(lightInner, 1.0, length(lp) / lightRadius);
    vec3 lightDir = normalize(lp);
    vec2 bln = blinn(lightDir, normal, view);
    vec4 lc = lit(bln.x, bln.y, 1.0);
    return lightRgb * clamp(lc.y, 0.0, 1.0) * attn;
}

void main()
{
    vec3 normal = texture(gNormal, v_texcoord).xyz * 2.0 - 1.0;
    float deviceDepth = texture(gDepth, v_texcoord).x;

    vec3 clip = vec3(v_texcoord * 2.0 - 1.0, deviceDepth);
    vec4 wpos4 = fsp.inv_mvp * vec4(clip, 1.0);
    vec3 wpos  = wpos4.xyz / wpos4.w;

    vec3 view = -normalize((fsp.view * vec4(wpos, 0.0)).xyz);

    vec3 lightColor = calcLight(wpos, normal, view,
                                fpc.light_pos_radius.xyz, fpc.light_pos_radius.w,
                                fpc.light_rgb_inner_r.xyz, fpc.light_rgb_inner_r.w);

    outColor = vec4(pow(lightColor, vec3(1.0 / 2.2)), 1.0);
}
