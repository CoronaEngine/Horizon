#version 450

// 移植自参考示例 21-deferred 的 fs_deferred_geom.sc（世界法线分量）：
// 法线贴图经 TBN 变换到世界空间后 encodeNormalUint 写入。

layout(binding = 2) uniform sampler2D texColor;
layout(binding = 3) uniform sampler2D texNormal;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_tangent;
layout(location = 2) in vec3 v_bitangent;
layout(location = 3) in vec2 v_texcoord;

layout(location = 0) out vec4 outNormal;

void main()
{
    vec3 normal;
    normal.xy = texture(texNormal, v_texcoord).xy * 2.0 - 1.0;
    normal.z = sqrt(max(0.0, 1.0 - dot(normal.xy, normal.xy)));

    mat3 tbn = mat3(normalize(v_tangent), normalize(v_bitangent), normalize(v_normal));
    vec3 wnormal = normalize(tbn * normal);

    outNormal = vec4(wnormal * 0.5 + 0.5, 1.0); // encodeNormalUint
}
