#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

// Stream compaction + fill VkDrawIndexedIndirectCommand buffer.
// Based on Mark Harris parallel scan (bgfx cs_gdr_stream_compaction).

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;

layout(set = 1, binding = 0) readonly buffer ConstSSBO
{
    uint data[];
} constBuffers[];

layout(set = 1, binding = 0) readonly buffer InstanceInSSBO
{
    vec4 data[];
} instanceInBuffers[];

layout(set = 1, binding = 0) readonly buffer PredicateSSBO
{
    uint data[];
} predicateBuffers[];

layout(set = 1, binding = 0) buffer CountSSBO
{
    uint data[];
} countBuffers[];

// Flat uints avoid std430 struct-array padding so stride stays 20 bytes
// (VkDrawIndexedIndirectCommand).
layout(set = 1, binding = 0) buffer IndirectSSBO
{
    uint data[];
} indirectBuffers[];

layout(set = 1, binding = 0) buffer InstanceOutSSBO
{
    vec4 data[];
} instanceOutBuffers[];

layout(set = 3, binding = 0) uniform CompactUniforms
{
    vec4 cullingConfig; // x=totalInstances, y=instancesPow2, z=unused, w=numDrawCalls
} u;

layout(push_constant) uniform PushConsts
{
    uint constBufferID;
    uint instanceInID;
    uint predicateID;
    uint countBufferID;
    uint indirectBufferID;
    uint instanceOutID;
    uint _pad0;
    uint _pad1;
} pc;

shared uint temp[2048];

void main()
{
    uint tID = gl_LocalInvocationID.x;
    int noofInstancesPow2 = int(u.cullingConfig.y);
    int noofDrawcalls = int(u.cullingConfig.w);

    int offset = 1;
    bool predicate0 = predicateBuffers[pc.predicateID].data[2u * tID] != 0u;
    temp[2u * tID] = predicate0 ? 1u : 0u;

    bool predicate1 = predicateBuffers[pc.predicateID].data[2u * tID + 1u] != 0u;
    temp[2u * tID + 1u] = predicate1 ? 1u : 0u;

    for (int d = noofInstancesPow2 >> 1; d > 0; d >>= 1)
    {
        barrier();
        if (int(tID) < d)
        {
            int ai = offset * (2 * int(tID) + 1) - 1;
            int bi = offset * (2 * int(tID) + 2) - 1;
            temp[bi] += temp[ai];
        }
        offset *= 2;
    }

    if (tID == 0u)
        temp[noofInstancesPow2 - 1] = 0u;

    for (int d = 1; d < noofInstancesPow2; d *= 2)
    {
        offset >>= 1;
        barrier();
        if (int(tID) < d)
        {
            int ai = offset * (2 * int(tID) + 1) - 1;
            int bi = offset * (2 * int(tID) + 2) - 1;
            uint t = temp[ai];
            temp[ai] = temp[bi];
            temp[bi] += t;
        }
    }

    barrier();

    int index = int(2u * tID);
    if (predicateBuffers[pc.predicateID].data[index] != 0u)
    {
        uint dst = temp[index];
        instanceOutBuffers[pc.instanceOutID].data[4u * dst + 0u] =
            instanceInBuffers[pc.instanceInID].data[4u * uint(index) + 0u];
        instanceOutBuffers[pc.instanceOutID].data[4u * dst + 1u] =
            instanceInBuffers[pc.instanceInID].data[4u * uint(index) + 1u];
        instanceOutBuffers[pc.instanceOutID].data[4u * dst + 2u] =
            instanceInBuffers[pc.instanceInID].data[4u * uint(index) + 2u];
        instanceOutBuffers[pc.instanceOutID].data[4u * dst + 3u] =
            instanceInBuffers[pc.instanceInID].data[4u * uint(index) + 3u];
    }

    index = int(2u * tID + 1u);
    if (predicateBuffers[pc.predicateID].data[index] != 0u)
    {
        uint dst = temp[index];
        instanceOutBuffers[pc.instanceOutID].data[4u * dst + 0u] =
            instanceInBuffers[pc.instanceInID].data[4u * uint(index) + 0u];
        instanceOutBuffers[pc.instanceOutID].data[4u * dst + 1u] =
            instanceInBuffers[pc.instanceInID].data[4u * uint(index) + 1u];
        instanceOutBuffers[pc.instanceOutID].data[4u * dst + 2u] =
            instanceInBuffers[pc.instanceInID].data[4u * uint(index) + 2u];
        instanceOutBuffers[pc.instanceOutID].data[4u * dst + 3u] =
            instanceInBuffers[pc.instanceInID].data[4u * uint(index) + 3u];
    }

    if (tID == 0u)
    {
        uint startInstance = 0u;
        for (int k = 0; k < noofDrawcalls; ++k)
        {
            uint base = uint(k) * 5u;
            indirectBuffers[pc.indirectBufferID].data[base + 0u] =
                constBuffers[pc.constBufferID].data[k * 3 + 0];
            indirectBuffers[pc.indirectBufferID].data[base + 1u] =
                countBuffers[pc.countBufferID].data[k];
            indirectBuffers[pc.indirectBufferID].data[base + 2u] =
                constBuffers[pc.constBufferID].data[k * 3 + 1];
            indirectBuffers[pc.indirectBufferID].data[base + 3u] =
                constBuffers[pc.constBufferID].data[k * 3 + 2];
            indirectBuffers[pc.indirectBufferID].data[base + 4u] = startInstance;

            startInstance += countBuffers[pc.countBufferID].data[k];
            countBuffers[pc.countBufferID].data[k] = 0u;
        }
    }
}
