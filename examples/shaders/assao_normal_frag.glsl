#version 450

// 39-assao G-buffer 法线 pass：view 空间法线编码到 RGBA8。
// UBO 同 assao_scene_vert.glsl（AssaoSceneShared）；color 在 PC 中但本 pass 不用。

layout(binding = 0) uniform AssaoSceneShared {
    mat4 proj_view;
    mat4 view_matrix;
} fsp;

layout(push_constant) uniform AssaoScenePC {
    mat4 model;
    vec4 color;
} pc;

layout(location = 0) in vec3 v_normal_vs;

layout(location = 0) out vec4 outNormal;

void main()
{
    outNormal = vec4(normalize(v_normal_vs) * 0.5 + 0.5, 1.0);
}
