#version 450

// 阴影体示例 pass 4：点光源漫反射 × 阴影可见性，加法混合叠到 ambient 上
// （clear_color_target=false）。可见性来自计数纹理：计数为 0 = 不在阴影里。

layout(binding = 0) uniform SvSceneParams {
    mat4 mvp;
    mat4 model_view;
    vec4 light_pos_vs;
    vec4 ambient;
    vec4 diffuse;
    vec4 color;
    vec4 params; // xy: 屏幕分辨率
} fsp;

layout(binding = 2) uniform sampler2D shadowCount;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_view;

layout(location = 0) out vec4 outColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / fsp.params.xy;
    float count = texture(shadowCount, uv).x;
    float visibility = abs(count) < 0.5 ? 1.0 : 0.0;

    vec3 n = normalize(v_normal);
    vec3 ld = normalize(fsp.light_pos_vs.xyz - v_view);
    float ndotl = max(0.0, dot(n, ld));

    vec3 rgb = fsp.diffuse.xyz * fsp.color.xyz * ndotl * visibility;
    outColor = vec4(rgb, 1.0);
}
