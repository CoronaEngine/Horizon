#version 450

// 移植自 bgfx examples/17-drawstress 的 vs_drawstress.sc：
// 每个 draw 逐次更新 mvp（走 record() 的逐 draw uniform 快照路径，压测提交开销）。

layout(binding = 0) uniform DrawStressParams {
    mat4 mvp;
} dsp;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 v_color;

void main()
{
    gl_Position = dsp.mvp * vec4(inPosition, 1.0);
    v_color = inColor;
}
