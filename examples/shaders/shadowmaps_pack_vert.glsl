#version 450

// 移植自参考示例 16-shadowmaps 的 vs_shadowmaps_packdepth.sc：
// 光源视角渲染，crip 空间坐标传给 FS 打包深度。
// mvp = light_proj * light_view * model，per-draw，64 bytes → push constant。

layout(push_constant) uniform ShadowPackPC {
    mat4 mvp;
} model_pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal; // 占位对齐顶点布局

layout(location = 0) out vec4 v_position;

void main()
{
    gl_Position = model_pc.mvp * vec4(inPosition, 1.0);
    v_position  = gl_Position;
}
