#version 450

// 39-assao G-buffer 法线 pass：view 空间法线编码到 RGBA8。

layout(binding = 0) uniform AssaoSceneParams {
    mat4 mvp;
    mat4 model_view;
    vec4 color;
} fsp;

layout(location = 0) in vec3 v_normal_vs;

layout(location = 0) out vec4 outNormal;

void main()
{
    outNormal = vec4(normalize(v_normal_vs) * 0.5 + 0.5, 1.0);
}
