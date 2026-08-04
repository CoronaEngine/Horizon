#version 450

// 阴影体示例 pass 1：环境光底色（同时填充本 pass 的深度附件）。
// UBO 同 sv_scene_vert.glsl（SvSceneShared），FS 不依赖 per-draw pc.model。

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform SvSceneShared {
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
    float z         = length(v_view);
    float fogFactor = clamp(1.0 / exp2(fsp.fog.w * fsp.fog.w * z * z * 1.442695), 0.0, 1.0);
    outColor = vec4(fsp.ambient.xyz * fsp.color.xyz * fogFactor, 1.0);
}
