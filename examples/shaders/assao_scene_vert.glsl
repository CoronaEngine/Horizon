#version 450
#include "raster_batch.slang"
layout(location = 15) flat out uint drawParameterWord;

// 移植自参考示例 39-assao 的 vs_assao_gbuffer.sc。
//
// 拆分策略：
//   - UBO  vsp：proj_view + view_matrix（相机矩阵，批次共享）128 bytes
//   - BDA 参数 ：model + color（per-draw；不同物体颜色/位置各异）80 bytes

layout(set = 3, binding = 0) uniform AssaoSceneShared {
    mat4 proj_view;
    mat4 view_matrix;
} vsp;

struct AssaoScenePC {
    mat4 model; // per-draw 变换
    vec4 color; // per-draw 物体底色（写入绘制参数，避免 UBO 写入 per-draw 数据）
};
#define vpc (*(AssaoScenePC*)(drawRoot.parameters + drawParameterWord))

layout(location = 0) out vec3 v_normal_vs;

void main()
{
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    drawParameterWord = packet.parameterWord;
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    vec3 inPosition = *(vec3*)(vertex + 0);
    vec3 inNormal = *(vec3*)(vertex + 3);
    mat4 mvp        = vsp.proj_view   * vpc.model;
    mat4 model_view = vsp.view_matrix * vpc.model;

    gl_Position  = mvp        * vec4(inPosition, 1.0);
    v_normal_vs  = normalize((model_view * vec4(inNormal, 0.0)).xyz);
}
