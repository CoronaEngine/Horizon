#version 450

// 移植自参考示例 21-deferred 的 vs_deferred_combine.sc：全屏 quad。

layout(location = 0) in vec3 inCorner; // (0,0)..(1,1)

layout(location = 0) out vec2 v_texcoord;

void main()
{
    gl_Position = vec4(inCorner.xy * 2.0 - 1.0, 0.0, 1.0);
    v_texcoord = inCorner.xy;
}
