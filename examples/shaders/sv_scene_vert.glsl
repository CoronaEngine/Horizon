#version 450

// 阴影体示例：场景通用 VS（ambient / depthval / lit 三个 pass 共用布局）。
//
// 拆分策略：
//   - UBO  vsp：批次共享（相机矩阵 + 所有光源/材质参数）256 bytes
//   - PC   pc ：per-draw（model，每个物体不同）                 64 bytes

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
    vec4 params; // xy: 屏幕分辨率
} vsp;

// vert/frag 共享同一 push constant 块。shadowCountIndex 仅 lit pass 的 FS 使用，
// ambient/depthval pass 忽略；三 pass 共用 vert，布局必须一致。
layout(push_constant) uniform SvScenePC {
    mat4 model;
    uint shadowCountIndex;
} model_pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_view;

void main()
{
    mat4 mvp        = vsp.proj_view   * model_pc.model;
    mat4 model_view = vsp.view_matrix * model_pc.model;

    gl_Position = mvp        * vec4(inPosition, 1.0);
    v_normal    = normalize((model_view * vec4(inNormal,   0.0)).xyz);
    v_view      = (model_view * vec4(inPosition, 1.0)).xyz;
}
