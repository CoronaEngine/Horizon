#version 450

// 阴影体示例：场景通用 VS（ambient / depthval / lit 三个 pass 共用布局）。
//
// 拆分策略：
//   - UBO  vsp：批次共享（相机矩阵 + 所有光源/材质参数）256 bytes
//   - PC   pc ：per-draw（model，每个物体不同）                 64 bytes

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
    vec4 params; // xy: 屏幕分辨率
} vsp;

layout(push_constant) uniform SvScenePC {
    mat4 model;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_view;

void main()
{
    mat4 mvp        = vsp.proj_view   * pc.model;
    mat4 model_view = vsp.view_matrix * pc.model;

    gl_Position = mvp        * vec4(inPosition, 1.0);
    v_normal    = normalize((model_view * vec4(inNormal,   0.0)).xyz);
    v_view      = (model_view * vec4(inPosition, 1.0)).xyz;
}
