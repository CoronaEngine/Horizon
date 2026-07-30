#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

layout(set = 1, binding = 0) readonly buffer BBoxSSBO
{
    vec4 data[];
} bboxBuffers[];

layout(set = 1, binding = 0) buffer CountSSBO
{
    uint data[];
} countBuffers[];

layout(set = 1, binding = 0) buffer PredicateSSBO
{
    uint data[];
} predicateBuffers[];

layout(set = 3, binding = 0) uniform OccludeUniforms
{
    mat4 viewProj;
    vec4 inputRTSize;     // xy = hiZ size, zw = 1/size
    vec4 cullingConfig;   // x=totalInstances, y=instancesPow2, z=numMips-1 max, w=numDrawCalls
    uvec4 hiZMipIDs0;     // mip IDs 0..3
    uvec4 hiZMipIDs1;     // mip IDs 4..7
    uvec4 hiZMipIDs2;     // mip IDs 8..11
    uvec4 hiZMipIDs3;     // mip IDs 12..15
} u;

layout(push_constant) uniform PushConsts
{
    uint bboxBufferID;
    uint countBufferID;
    uint predicateBufferID;
    uint _pad0;
} pc;

uint hi_z_mip_id(uint mipIndex)
{
    switch (mipIndex / 4u)
    {
    default:
    case 0u: return u.hiZMipIDs0[mipIndex % 4u];
    case 1u: return u.hiZMipIDs1[mipIndex % 4u];
    case 2u: return u.hiZMipIDs2[mipIndex % 4u];
    case 3u: return u.hiZMipIDs3[mipIndex % 4u];
    }
}

float sample_hiz(vec2 uv, float mip)
{
    uint mipIndex = uint(clamp(mip, 0.0, u.cullingConfig.z));
    uint texID = hi_z_mip_id(mipIndex);
    return texture(combinedTextureSamplerHandles[texID], uv).x;
}

void main()
{
    uint instanceId = gl_GlobalInvocationID.x;
    uint totalInstances = uint(u.cullingConfig.x);
    if (instanceId >= totalInstances)
        return;

    vec4 bboxMin = bboxBuffers[pc.bboxBufferID].data[2u * instanceId];
    vec3 bboxMax = bboxBuffers[pc.bboxBufferID].data[2u * instanceId + 1u].xyz;
    int drawcallID = int(bboxMin.w);

    vec3 bboxSize = bboxMax - bboxMin.xyz;
    vec3 boxCorners[8] = vec3[](
        bboxMin.xyz,
        bboxMin.xyz + vec3(bboxSize.x, 0.0, 0.0),
        bboxMin.xyz + vec3(0.0, bboxSize.y, 0.0),
        bboxMin.xyz + vec3(0.0, 0.0, bboxSize.z),
        bboxMin.xyz + vec3(bboxSize.xy, 0.0),
        bboxMin.xyz + vec3(0.0, bboxSize.yz),
        bboxMin.xyz + vec3(bboxSize.x, 0.0, bboxSize.z),
        bboxMin.xyz + bboxSize);

    float minZ = 1.0;
    vec2 minXY = vec2(1.0);
    vec2 maxXY = vec2(0.0);

    for (int i = 0; i < 8; ++i)
    {
        vec4 clipPos = u.viewProj * vec4(boxCorners[i], 1.0);
        clipPos.z = max(clipPos.z, 0.0);
        clipPos.xyz /= clipPos.w;
        clipPos.xy = clamp(clipPos.xy, vec2(-1.0), vec2(1.0));
        clipPos.xy = clipPos.xy * vec2(0.5, -0.5) + vec2(0.5, 0.5);

        minXY = min(clipPos.xy, minXY);
        maxXY = max(clipPos.xy, maxXY);
        minZ = clamp(min(minZ, clipPos.z), 0.0, 1.0);
    }

    vec4 boxUVs = vec4(minXY, maxXY);
    ivec2 size = ivec2((maxXY - minXY) * u.inputRTSize.xy);
    float mip = ceil(log2(max(float(max(size.x, size.y)), 1.0)));
    mip = clamp(mip, 0.0, u.cullingConfig.z);

    float levelLower = max(mip - 1.0, 0.0);
    vec2 scale = vec2(exp2(-levelLower));
    vec2 a = floor(boxUVs.xy * scale);
    vec2 b = ceil(boxUVs.zw * scale);
    vec2 dims = b - a;
    if (dims.x <= 2.0 && dims.y <= 2.0)
        mip = levelLower;

    boxUVs.y = 1.0 - boxUVs.y;
    boxUVs.w = 1.0 - boxUVs.w;

    vec4 depth = vec4(
        sample_hiz(boxUVs.xy, mip),
        sample_hiz(boxUVs.zy, mip),
        sample_hiz(boxUVs.xw, mip),
        sample_hiz(boxUVs.zw, mip));

    float maxDepth = max(max(depth.x, depth.y), max(depth.z, depth.w));
    bool visible = minZ <= maxDepth;

    predicateBuffers[pc.predicateBufferID].data[instanceId] = visible ? 1u : 0u;
    if (visible)
        atomicAdd(countBuffers[pc.countBufferID].data[drawcallID], 1u);
}
