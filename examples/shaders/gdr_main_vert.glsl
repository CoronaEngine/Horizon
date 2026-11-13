#version 450
#include "raster_batch.slang"
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

layout(set = 1, binding = 0) readonly buffer InstanceSSBO
{
    vec4 data[];
} instanceBuffers[];

layout(set = 3, binding = 0) uniform Scene {
    mat4 view;
    mat4 proj;
    uint instanceBufferID;
    vec4 color0;
    vec4 color1;
    vec4 color2;
    vec4 color3;
} vp;

// flat int: material id must not be perspectively interpolated as float
// (int(0.999) truncates to 0 → wrong color0 on some pixels).
layout(location = 0) flat out int v_materialID;

void main()
{
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    vec3 inPosition = *(vec3*)(vertex + 0);
    uint base = gl_InstanceIndex * 4u;
    vec4 r0 = instanceBuffers[vp.instanceBufferID].data[base + 0u];
    vec4 r1 = instanceBuffers[vp.instanceBufferID].data[base + 1u];
    vec4 r2 = instanceBuffers[vp.instanceBufferID].data[base + 2u];
    vec4 r3 = instanceBuffers[vp.instanceBufferID].data[base + 3u];

    mat4 model = mat4(vec4(r0.xyz, 0.0), r1, r2, r3);

    gl_Position = vp.proj * vp.view * model * vec4(inPosition, 1.0);
    v_materialID = int(r0.w + 0.5);
}
