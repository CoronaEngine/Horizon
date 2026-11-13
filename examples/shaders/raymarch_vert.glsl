#version 450
#include "raster_batch.slang"

// 移植自参考示例 03-raymarch 的 vs_raymarching.sc：
// 顶点直接给的是 NDC 坐标，不再需要 ortho 矩阵；uv 即 NDC xy。

layout(location = 0) out vec3 v_color;
layout(location = 1) out vec2 v_texcoord;

void main()
{
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    vec3 inPosition = *(vec3*)(vertex + 0);
    vec3 inColor = *(vec3*)(vertex + 3);
    vec2 inTexCoord = *(vec2*)(vertex + 6);
    gl_Position = vec4(inPosition, 1.0);
    v_color = inColor;
    v_texcoord = inTexCoord;
}
