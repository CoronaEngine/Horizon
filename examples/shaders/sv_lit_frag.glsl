#version 450

// 阴影体示例 pass 4：点光源漫反射 × 阴影可见性。

#extension GL_EXT_nonuniform_qualifier : enable

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform SvSceneShared {
    mat4 proj_view;
    mat4 view_matrix;
    vec4 light_pos_vs;
    vec4 light_rgb;
    vec4 ambient;
    vec4 diffuse;
    vec4 specular_shininess;
    vec4 fog;
    vec4 color;
    vec4 params;
} fsp;

// bindless combined-image-sampler 表（set 0）+ 与 sv_scene_vert.glsl 共享的 push constant。
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

// 与 sv_scene_vert.glsl 共享同一 push constant 块（布局一致）。EDSL 同时继承 vert/frag
// 的 ResourceBindings，两 stage 实例名必须不同以避免 C++ 端二义（C2385）。
// C++ 通过 vert 的 model_pc 写入，frag 用 model_pc_fs 只读。
layout(push_constant) uniform SvScenePC {
    mat4 model;
    uint shadowCountIndex;
} model_pc_fs;

#define shadowCount combinedTextureSamplerHandles[model_pc_fs.shadowCountIndex]

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_view;

layout(location = 0) out vec4 outColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / fsp.params.xy;
    float count = texture(shadowCount, uv).x;
    float visibility = abs(count) < 0.5 ? 1.0 : 0.0;

    vec3 n       = normalize(v_normal);
    vec3 viewDir = -normalize(v_view);
    vec3 toLight = fsp.light_pos_vs.xyz - v_view;
    vec3 ld      = normalize(toLight);

    float ndotl   = dot(n, ld);
    vec3 reflected = 2.0 * ndotl * n - ld;
    float rdotv   = dot(reflected, viewDir);
    float diff    = max(0.0, ndotl);
    float spec    = step(0.0, ndotl) * pow(max(0.0, rdotv), fsp.specular_shininess.w);

    float dist = max(length(toLight), fsp.light_pos_vs.w);
    float attn = 50.0 * pow(dist, -2.0);
    vec3 rgb   = (diff * fsp.diffuse.xyz + spec * fsp.specular_shininess.xyz) * fsp.light_rgb.xyz * attn;

    float z         = length(v_view);
    float fogFactor = clamp(1.0 / exp2(fsp.fog.w * fsp.fog.w * z * z * 1.442695), 0.0, 1.0);

    outColor = vec4(rgb * fsp.color.xyz * visibility * fogFactor, 1.0);
}
