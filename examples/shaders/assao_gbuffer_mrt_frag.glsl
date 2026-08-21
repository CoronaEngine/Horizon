#version 450

// 39-assao G-buffer MRT pass：单次渲染输出 color/normal/depth 到三个 RT。
// 合并原 assao_color_frag.glsl + assao_normal_frag.glsl + assao_depth_frag.glsl。

layout(binding = 0) uniform AssaoSceneShared {
    mat4 proj_view;
    mat4 view_matrix;
} fsp;

layout(push_constant) uniform AssaoScenePC {
    mat4 model;
    vec4 color;
} fpc;

layout(location = 0) in vec3 v_normal_vs;

// MRT 输出：3个颜色附件
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outDepthVal;

void main()
{
    vec3 n = normalize(v_normal_vs);

    // RT0: 场景颜色（固定方向光照）
    const vec3 light_dir_vs = normalize(vec3(-0.3, 0.8, -0.5));
    float ndotl = max(0.0, dot(n, light_dir_vs));
    outColor = vec4(fpc.color.xyz * (0.25 + 0.75 * ndotl), 1.0);

    // RT1: view 空间法线编码到 [0,1]
    outNormal = vec4(n * 0.5 + 0.5, 1.0);

    // RT2: 器件深度值
    outDepthVal = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
