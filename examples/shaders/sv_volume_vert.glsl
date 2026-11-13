#version 450
#include "raster_batch.slang"
layout(location = 15) flat out uint drawParameterWord;

// 阴影体示例 pass 3：CPU 生成的世界空间阴影体（轮廓边挤出），只需 view_proj。

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform SvVolumeParams {
    mat4 view_proj;
    vec4 params; // xy: 屏幕分辨率
} vvp;

// vert/frag 共享同一 BDA 绘制参数。sceneDepthIndex 仅 FS 使用。
struct SvVolumePC {
    uint sceneDepthIndex;
};
#define volume_pc (*(SvVolumePC*)(drawRoot.parameters + drawParameterWord))

void main()
{
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    drawParameterWord = packet.parameterWord;
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    vec3 inPosition = *(vec3*)(vertex + 0);
    gl_Position = vvp.view_proj * vec4(inPosition, 1.0);
}
