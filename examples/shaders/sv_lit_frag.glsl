#version 450

// 阴影体示例 pass 4：点光源漫反射 × 阴影可见性，加法混合叠到 ambient 上
// （clear_color_target=false）。可见性来自计数纹理：计数为 0 = 不在阴影里。

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

layout(binding = 2) uniform sampler2D shadowCount;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_view;

layout(location = 0) out vec4 outColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / fsp.params.xy;
    float count = texture(shadowCount, uv).x;
    float visibility = abs(count) < 0.5 ? 1.0 : 0.0;

    // 对齐原版 fs_shadowvolume_color_lighting.sc：blinn + lit + 距离衰减 + 镜面 + 雾
    vec3 n = normalize(v_normal);
    vec3 viewDir = -normalize(v_view);
    vec3 toLight = fsp.light_pos_vs.xyz - v_view;
    vec3 ld = normalize(toLight);

    float ndotl = dot(n, ld);
    vec3 reflected = 2.0 * ndotl * n - ld;
    float rdotv = dot(reflected, viewDir);
    float diff = max(0.0, ndotl);
    float spec = step(0.0, ndotl) * pow(max(0.0, rdotv), fsp.specular_shininess.w);

    float dist = max(length(toLight), fsp.light_pos_vs.w);
    float attn = 50.0 * pow(dist, -2.0);
    vec3 rgb = (diff * fsp.diffuse.xyz + spec * fsp.specular_shininess.xyz) * fsp.light_rgb.xyz * attn;

    float z = length(v_view);
    float fogFactor = clamp(1.0 / exp2(fsp.fog.w * fsp.fog.w * z * z * 1.442695), 0.0, 1.0);

    outColor = vec4(rgb * fsp.color.xyz * visibility * fogFactor, 1.0);
}
