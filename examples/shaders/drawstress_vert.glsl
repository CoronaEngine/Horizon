#version 450

// 移植自 bgfx examples/17-drawstress 的 vs_drawstress.sc：
// 每个 draw 逐次更新 mvp —— 64 bytes 完全符合 push constant，
// 不需要 UBO，避免每 draw 创建 VkBuffer 的开销。

layout(push_constant) uniform DrawStressPC {
    mat4 mvp;
} model_pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 v_color;

void main()
{
    gl_Position = model_pc.mvp * vec4(inPosition, 1.0);
    v_color = inColor;
}
