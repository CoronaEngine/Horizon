#version 450
#include "raster_batch.slang"
layout(location = 0) out vec3 v_color;
void main() {
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    mat4 mvp = *(mat4*)(drawRoot.parameters + packet.parameterWord);
    gl_Position = mvp * vec4(*(vec3*)vertex, 1.0);
    v_color = *(vec3*)(vertex + 3);
}
