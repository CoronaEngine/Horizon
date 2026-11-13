#version 450
#include "raster_batch.slang"
layout(location = 15) flat out uint drawParameterWord;

// 移植自参考示例 21-deferred 的 vs_deferred_geom.sc。
//
// 拆分策略：
//   - UBO  vsp：view_proj（共享，所有 cube 相同）  64 bytes
//   - BDA 参数 ：model（per-draw，每 cube 不同）     64 bytes

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform DeferredGeomShared {
    mat4 view_proj;
} vsp;

// vert/frag 共享同一 BDA 绘制参数，布局必须一致。texColorIndex/texNormalIndex 仅 FS 使用。
struct DeferredGeomPC {
    mat4 model;
    uint texColorIndex;
    uint texNormalIndex;
};
#define model_pc (*(DeferredGeomPC*)(drawRoot.parameters + drawParameterWord))

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_tangent;
layout(location = 2) out vec3 v_bitangent;
layout(location = 3) out vec2 v_texcoord;

void main()
{
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    drawParameterWord = packet.parameterWord;
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    vec3 inPosition = *(vec3*)(vertex + 0);
    vec3 inNormal = *(vec3*)(vertex + 3);
    vec4 inTangent = *(vec4*)(vertex + 6);
    vec2 inTexCoord = *(vec2*)(vertex + 10);
    vec3 wpos = (model_pc.model * vec4(inPosition, 1.0)).xyz;
    gl_Position = vsp.view_proj * vec4(wpos, 1.0);

    vec3 wnormal  = normalize((model_pc.model * vec4(inNormal,      0.0)).xyz);
    vec3 wtangent = normalize((model_pc.model * vec4(inTangent.xyz, 0.0)).xyz);

    v_normal    = wnormal;
    v_tangent   = wtangent;
    v_bitangent = cross(wnormal, wtangent) * inTangent.w;
    v_texcoord  = inTexCoord;
}
