#version 450

// 移植自参考示例 21-deferred 的 vs_deferred_geom.sc。
// 简化：TBN 直接在世界空间构建（原版绕道 view 空间再用 invView 转回，
// 结果等价——G-buffer 里存的都是世界空间法线）。

layout(binding = 0) uniform DeferredGeomParams {
    mat4 model;
    mat4 view_proj;
} vsp;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent; // w: bitangent 符号
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_tangent;
layout(location = 2) out vec3 v_bitangent;
layout(location = 3) out vec2 v_texcoord;

void main()
{
    vec3 wpos = (vsp.model * vec4(inPosition, 1.0)).xyz;
    gl_Position = vsp.view_proj * vec4(wpos, 1.0);

    vec3 wnormal = normalize((vsp.model * vec4(inNormal, 0.0)).xyz);
    vec3 wtangent = normalize((vsp.model * vec4(inTangent.xyz, 0.0)).xyz);

    v_normal = wnormal;
    v_tangent = wtangent;
    v_bitangent = cross(wnormal, wtangent) * inTangent.w;
    v_texcoord = inTexCoord;
}
