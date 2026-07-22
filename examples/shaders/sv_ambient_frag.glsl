#version 450

// 阴影体示例 pass 1：环境光底色（同时填充本 pass 的深度附件）。

layout(binding = 0) uniform SvSceneParams {
    mat4 mvp;
    mat4 model_view;
    vec4 light_pos_vs;
    vec4 light_rgb;
    vec4 ambient;
    vec4 diffuse;
    vec4 specular_shininess;
    vec4 fog;
    vec4 color;
    vec4 params;
} fsp;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_view;

layout(location = 0) out vec4 outColor;

void main()
{
    // 指数雾对齐原版：final = mix(fogColor, color, fogFactor)，雾色为黑
    float z = length(v_view);
    float fogFactor = clamp(1.0 / exp2(fsp.fog.w * fsp.fog.w * z * z * 1.442695), 0.0, 1.0);
    outColor = vec4(fsp.ambient.xyz * fsp.color.xyz * fogFactor, 1.0);
}
