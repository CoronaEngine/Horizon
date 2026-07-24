#version 450

// 移植自参考示例 39-assao 的 vs_assao_gbuffer.sc。
//
// 拆分策略：
//   - UBO  vsp：proj_view + view_matrix（相机矩阵，批次共享）128 bytes
//   - PC   pc ：model + color（per-draw；不同物体颜色/位置各异）80 bytes

layout(binding = 0) uniform AssaoSceneShared {
    mat4 proj_view;
    mat4 view_matrix;
} vsp;

layout(push_constant) uniform AssaoScenePC {
    mat4 model; // per-draw 变换
    vec4 color; // per-draw 物体底色（压入 PC，避免 UBO 写入 per-draw 数据）
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 v_normal_vs;

void main()
{
    mat4 mvp        = vsp.proj_view   * pc.model;
    mat4 model_view = vsp.view_matrix * pc.model;

    gl_Position  = mvp        * vec4(inPosition, 1.0);
    v_normal_vs  = normalize((model_view * vec4(inNormal, 0.0)).xyz);
}
