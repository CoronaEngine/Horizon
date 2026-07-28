#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

layout(set = 1, binding = 0) readonly buffer InstanceSSBO
{
    vec4 data[];
} instanceBuffers[];

layout(set = 3, binding = 0) uniform ViewProj
{
    mat4 view;
    mat4 proj;
} vp;

layout(push_constant) uniform PushConsts
{
    uint instanceBufferID;
    uint _pad0;
    uint _pad1;
    uint _pad2;
} pc;

layout(location = 0) in vec3 inPosition;

// flat int: material id must not be perspectively interpolated as float
// (int(0.999) truncates to 0 ΓåÆ wrong color0 on some pixels).
layout(location = 0) flat out int v_materialID;

void main()
{
    uint base = gl_InstanceIndex * 4u;
    vec4 r0 = instanceBuffers[pc.instanceBufferID].data[base + 0u];
    vec4 r1 = instanceBuffers[pc.instanceBufferID].data[base + 1u];
    vec4 r2 = instanceBuffers[pc.instanceBufferID].data[base + 2u];
    vec4 r3 = instanceBuffers[pc.instanceBufferID].data[base + 3u];

    mat4 model = mat4(vec4(r0.xyz, 0.0), r1, r2, r3);

    gl_Position = vp.proj * vp.view * model * vec4(inPosition, 1.0);
    v_materialID = int(r0.w + 0.5);
}
