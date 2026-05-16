#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 2, binding = 0, rgba16f) uniform image2D inputImageRGBA16[];

layout(set = 3, binding = 0) uniform GlobalUniformParam
{
    uint imageID;
} globalParams;

vec3 acesFilmicToneMapCurve(vec3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;

    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 acesFilmicToneMapInverse(vec3 x)
{
    vec3 a = -0.59 * x + 0.03;
    vec3 b = sqrt(-1.0127 * x * x + 1.3702 * x + 0.0009);
    vec3 c = 2 * (2.43 * x - 2.51);
    return ((a - b) / c);
}

void main()
{
    uint imageID = globalParams.imageID;
    vec4 color = imageLoad(inputImageRGBA16[imageID], ivec2(gl_GlobalInvocationID.xy));

    imageStore(inputImageRGBA16[imageID], ivec2(gl_GlobalInvocationID.xy), vec4(acesFilmicToneMapCurve(color.xyz), 1.0));
}