#version 450

// 移植自参考示例 03-raymarch 的 vs_raymarching.sc：
// 顶点直接给的是 NDC 坐标，不再需要 ortho 矩阵；uv 即 NDC xy。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 v_color;
layout(location = 1) out vec2 v_texcoord;

void main()
{
    gl_Position = vec4(inPosition, 1.0);
    v_color = inColor;
    v_texcoord = inTexCoord;
}
