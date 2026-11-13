#version 450
#include "raster_batch.slang"

// 移植自参考示例 16-shadowmaps 的 vs_shadowmaps_packdepth.sc：
// 光源视角渲染，crip 空间坐标传给 FS 打包深度。
// mvp = light_proj * light_view * model，per-draw，64 bytes → BDA 绘制参数。

struct ShadowPackPC {
    mat4 mvp;
};
#define model_pc (*(ShadowPackPC*)(drawRoot.parameters + packet.parameterWord))

layout(location = 0) out vec4 v_position;

void main()
{
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    vec3 inPosition = *(vec3*)(vertex + 0);
    vec3 inNormal = *(vec3*)(vertex + 3);
    gl_Position = model_pc.mvp * vec4(inPosition, 1.0);
    v_position  = gl_Position;
}
