#version 450
#include "raster_batch.slang"
layout(location = 15) flat out uint hzParameterWord;

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform UiTransform {
    vec2 scale;
    vec2 translate;
} ubo;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    hzParameterWord = packet.parameterWord;
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    vec2 inPosition = *(vec2*)(vertex + 0);
    vec2 inTexCoord = *(vec2*)(vertex + 2);
    vec4 inColor = *(vec4*)(vertex + 4);

    gl_Position = vec4(inPosition * ubo.scale + ubo.translate, 0.0, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
