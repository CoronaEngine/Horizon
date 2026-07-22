#version 450

// 移植自参考示例 21-deferred 的 G-buffer 深度分量：器件深度写入 R32F
// （原版直接采样 depth attachment，这里显式输出等价）。

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_tangent;
layout(location = 2) in vec3 v_bitangent;
layout(location = 3) in vec2 v_texcoord;

layout(location = 0) out vec4 outDepthVal;

void main()
{
    outDepthVal = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
