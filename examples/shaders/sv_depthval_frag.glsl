#version 450

// 阴影体示例 pass 2：器件深度写入 R32F。

layout(binding = 0) uniform SvSceneShared {
    mat4 proj_view;
    mat4 view_matrix;
    vec4 light_pos_vs;
    vec4 light_rgb;
    vec4 ambient;
    vec4 diffuse;
    vec4 specular_shininess;
    vec4 fog;
    vec4 color;
    vec4 params;
} fsp;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_view;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
