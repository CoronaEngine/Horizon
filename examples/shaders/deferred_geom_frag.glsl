#version 450

// 移植自参考示例 21-deferred 的 fs_deferred_geom.sc（albedo 分量）。
// 框架动态渲染当前只绑定第一个颜色附件，G-buffer 的三张图拆成三个
// 几何 pass 分别输出（本 pass：albedo）。

layout(binding = 2) uniform sampler2D texColor;
layout(binding = 3) uniform sampler2D texNormal;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_tangent;
layout(location = 2) in vec3 v_bitangent;
layout(location = 3) in vec2 v_texcoord;

layout(location = 0) out vec4 outAlbedo;

void main()
{
    outAlbedo = texture(texColor, v_texcoord);
}
