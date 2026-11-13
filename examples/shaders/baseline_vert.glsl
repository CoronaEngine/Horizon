#version 450
#include "raster_batch.slang"
layout(location = 2) flat out uint drawParameterWord;

// VP：全局 uniform buffer。set 0-2 为 Horizon bindless 保留集（0=纹理/1=storage buffer/
// 2=storage image），普通 UBO 必须放在 set 3（与 EDSL bindless 约定一致）。
layout(set = 3, binding = 0) uniform ViewProj {
    mat4 view;
    mat4 proj;
} vp;

// BDA 绘制参数：per-draw 的 model 矩阵 + bindless 纹理索引（texIndex）。
// vert/frag 共享同一 BDA 绘制参数，布局必须一致。
struct PushConsts {
    mat4 model;
    uint texIndex;
};
#define pc (*(PushConsts*)(drawRoot.parameters + drawParameterWord))

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    drawParameterWord = packet.parameterWord;
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    vec3 inPosition = *(vec3*)(vertex + 0);
    vec3 inColor = *(vec3*)(vertex + 3);
    vec2 inTexCoord = *(vec2*)(vertex + 6);
    gl_Position = vp.proj * vp.view * pc.model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
