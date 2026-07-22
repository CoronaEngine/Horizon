#version 450

// 阴影体示例：场景通用 VS（ambient / depthval / lit 三个 pass 共用布局）。
// VS/FS 共用同一 layout 的 uniform block（单 set=0/binding=0），实例名 vsp/fsp。

layout(binding = 0) uniform SvSceneParams {
    mat4 mvp;
    mat4 model_view;
    vec4 light_pos_vs;  // view 空间光源位置
    vec4 ambient;       // rgb: 环境光
    vec4 diffuse;       // rgb: 漫反射光色
    vec4 color;         // rgb: 物体颜色
    vec4 params;        // xy: 屏幕分辨率
} vsp;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_view;

void main()
{
    gl_Position = vsp.mvp * vec4(inPosition, 1.0);
    v_normal = normalize((vsp.model_view * vec4(inNormal, 0.0)).xyz);
    v_view = (vsp.model_view * vec4(inPosition, 1.0)).xyz;
}
