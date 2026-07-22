#version 450

// 移植自参考示例 39-assao 的 vs_assao_gbuffer.sc：场景三个 G-buffer pass 共用。

layout(binding = 0) uniform AssaoSceneParams {
    mat4 mvp;
    mat4 model_view;
    vec4 color; // rgb: 物体底色
} vsp;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 v_normal_vs;

void main()
{
    gl_Position = vsp.mvp * vec4(inPosition, 1.0);
    v_normal_vs = normalize((vsp.model_view * vec4(inNormal, 0.0)).xyz);
}
