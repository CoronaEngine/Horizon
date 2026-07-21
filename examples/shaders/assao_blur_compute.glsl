#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

// 移植自参考示例 39-assao 的 cs_assao_smart_blur.sc 思路的简化实现：
// 3x3 深度边缘感知模糊（sharpness 控制跨边缘渗透），ping-pong 跑两遍。

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 2, binding = 0, r32f) uniform image2D imagesR32F[];

layout(push_constant) uniform PushConsts
{
    uint srcID;    // R32F AO 输入
    uint dstID;    // R32F AO 输出
    uint depthID;  // R32F 器件深度（边缘权重）
    uint pad0;
    vec4 params0;  // x: sharpness, yz: 分辨率
} pushConsts;

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    vec2 resolution = pushConsts.params0.yz;
    if (coord.x >= int(resolution.x) || coord.y >= int(resolution.y))
        return;

    float center_depth = imageLoad(imagesR32F[pushConsts.depthID], coord).x;
    float sharpness = pushConsts.params0.x;

    float sum = 0.0;
    float weight_sum = 0.0;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            ivec2 tap = clamp(coord + ivec2(dx, dy), ivec2(0), ivec2(resolution) - 1);
            float ao = imageLoad(imagesR32F[pushConsts.srcID], tap).x;
            float depth = imageLoad(imagesR32F[pushConsts.depthID], tap).x;

            float depth_diff = abs(depth - center_depth);
            float weight = 1.0 / (1.0 + depth_diff * sharpness * 4000.0);
            sum += ao * weight;
            weight_sum += weight;
        }
    }

    imageStore(imagesR32F[pushConsts.dstID], coord, vec4(sum / max(weight_sum, 0.0001), 0.0, 0.0, 0.0));
}
