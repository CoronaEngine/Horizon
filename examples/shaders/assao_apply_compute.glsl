#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

// 移植自参考示例 39-assao 的 cs_assao_apply.sc 思路的简化实现：
// 场景颜色 × AO → 最终输出。

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 2, binding = 0, r32f) uniform image2D imagesR32F[];
layout(set = 2, binding = 0, rgba16f) uniform image2D imagesRGBA16F[];

layout(push_constant) uniform PushConsts
{
    uint colorID;   // RGBA16F 场景颜色
    uint aoID;      // R32F AO
    uint outputID;  // RGBA16F 最终输出
    uint pad0;
    vec4 params0;   // xy: 分辨率
} pushConsts;

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= int(pushConsts.params0.x) || coord.y >= int(pushConsts.params0.y))
        return;

    vec3 color = imageLoad(imagesRGBA16F[pushConsts.colorID], coord).xyz;
    float ao = imageLoad(imagesR32F[pushConsts.aoID], coord).x;

    imageStore(imagesRGBA16F[pushConsts.outputID], coord, vec4(color * ao, 1.0));
}
