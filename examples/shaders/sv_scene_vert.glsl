#version 450
#include "raster_batch.slang"

// 阴影体示例：场景通用 VS（ambient / depthval / lit 三个 pass 共用布局）。
//
// 拆分策略：
//   - UBO  vsp：批次共享的相机、光源、材质和阴影纹理索引
//   - BDA 参数 ：per-draw（model，每个物体不同）                 64 bytes

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
    uint shadowCountIndex;
} vsp;

struct SvScenePC {
    mat4 model;
};
#define model_pc (*(SvScenePC*)(drawRoot.parameters + packet.parameterWord))

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_view;

void main()
{
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    vec3 inPosition = *(vec3*)(vertex + 0);
    vec3 inNormal = *(vec3*)(vertex + 3);
    mat4 mvp        = vsp.proj_view   * model_pc.model;
    mat4 model_view = vsp.view_matrix * model_pc.model;

    gl_Position = mvp        * vec4(inPosition, 1.0);
    v_normal    = normalize((model_view * vec4(inNormal,   0.0)).xyz);
    v_view      = (model_view * vec4(inPosition, 1.0)).xyz;
}
