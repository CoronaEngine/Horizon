#version 450

// 39-assao 场景颜色 pass：底色 × 固定方向光的简单光照（AO 由 compute 后乘）。

layout(binding = 0) uniform AssaoSceneParams {
    mat4 mvp;
    mat4 model_view;
    vec4 color;
} fsp;

layout(location = 0) in vec3 v_normal_vs;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 n = normalize(v_normal_vs);
    const vec3 light_dir_vs = normalize(vec3(-0.3, 0.8, -0.5));
    float ndotl = max(0.0, dot(n, light_dir_vs));
    outColor = vec4(fsp.color.xyz * (0.25 + 0.75 * ndotl), 1.0);
}
