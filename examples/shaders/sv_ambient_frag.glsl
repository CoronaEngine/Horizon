#version 450

// 阴影体示例 pass 1：环境光底色（同时填充本 pass 的深度附件）。

layout(binding = 0) uniform SvSceneParams {
    mat4 mvp;
    mat4 model_view;
    vec4 light_pos_vs;
    vec4 ambient;
    vec4 diffuse;
    vec4 color;
    vec4 params;
} fsp;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_view;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(fsp.ambient.xyz * fsp.color.xyz, 1.0);
}
