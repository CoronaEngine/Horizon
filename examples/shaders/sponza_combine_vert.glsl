#version 450
#include "raster_batch.slang"
layout(location = 15) flat out uint drawParameterWord;

// Sponza deferred combine, vertex stage: full-screen quad from (0,0)..(1,1).

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
