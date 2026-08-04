#version 450

// 移植自 bgfx examples/17-drawstress 的 fs_drawstress.sc：直通顶点色。

layout(location = 0) in vec3 v_color;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(v_color, 1.0);
}
