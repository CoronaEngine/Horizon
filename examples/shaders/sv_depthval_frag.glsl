#version 450

// 阴影体示例 pass 2：器件深度写入 R32F（供阴影体 pass 做逐像素深度剔除，
// 等价于对场景深度做只读深度测试）。

layout(binding = 0) uniform SvSceneParams {
    mat4 mvp;
    mat4 model_view;
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
