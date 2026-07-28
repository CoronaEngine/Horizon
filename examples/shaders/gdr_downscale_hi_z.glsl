#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(local_size_x = 16, local_size_y = 16) in;

layout(set = 2, binding = 0, r32f) uniform image2D imagesR32F[];

layout(push_constant) uniform PushConsts
{
    uint srcID;
    uint dstID;
    uint srcWidth;
    uint srcHeight;
} pc;

void main()
{
    ivec2 dstCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 dstSize = imageSize(imagesR32F[pc.dstID]);
    if (dstCoord.x >= dstSize.x || dstCoord.y >= dstSize.y)
        return;

    ivec2 srcCoord = dstCoord * 2;
    float d00 = imageLoad(imagesR32F[pc.srcID], min(srcCoord + ivec2(0, 0), ivec2(int(pc.srcWidth) - 1, int(pc.srcHeight) - 1))).r;
    float d10 = imageLoad(imagesR32F[pc.srcID], min(srcCoord + ivec2(1, 0), ivec2(int(pc.srcWidth) - 1, int(pc.srcHeight) - 1))).r;
    float d01 = imageLoad(imagesR32F[pc.srcID], min(srcCoord + ivec2(0, 1), ivec2(int(pc.srcWidth) - 1, int(pc.srcHeight) - 1))).r;
    float d11 = imageLoad(imagesR32F[pc.srcID], min(srcCoord + ivec2(1, 1), ivec2(int(pc.srcWidth) - 1, int(pc.srcHeight) - 1))).r;
    float maxDepth = max(max(d00, d10), max(d01, d11));
    imageStore(imagesR32F[pc.dstID], dstCoord, vec4(maxDepth, 0.0, 0.0, 1.0));
}
