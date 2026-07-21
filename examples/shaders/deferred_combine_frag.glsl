#version 450

// 移植自参考示例 21-deferred 的 fs_deferred_combine.sc：
// albedo × light（线性空间相乘再回 gamma）。

layout(binding = 2) uniform sampler2D gAlbedo;
layout(binding = 3) uniform sampler2D gLight;

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 albedo = pow(texture(gAlbedo, v_texcoord).xyz, vec3(2.2)); // toLinear
    vec3 light = pow(texture(gLight, v_texcoord).xyz, vec3(2.2));
    outColor = vec4(pow(albedo * light, vec3(1.0 / 2.2)), 1.0); // toGamma
}
