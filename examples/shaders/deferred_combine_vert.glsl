#version 450
#include "raster_batch.slang"
layout(location = 15) flat out uint drawParameterWord;

// 移植自参考示例 21-deferred 的 vs_deferred_combine.sc：全屏 quad。

layout(location = 0) out vec2 v_texcoord;

void main()
{
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    drawParameterWord = packet.parameterWord;
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    vec3 inCorner = *(vec3*)(vertex + 0);
    gl_Position = vec4(inCorner.xy * 2.0 - 1.0, 0.0, 1.0);
    v_texcoord = inCorner.xy;
}
