#version 450

#include "raster_batch.slang"

layout(location = 0) out vec3 fragColor;

void main()
{
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    vec3 inPosition = *(vec3*)vertex;
    vec3 inColor = *(vec3*)(vertex + 3);
    gl_Position = vec4(inPosition, 1.0);
    fragColor = inColor;
}
