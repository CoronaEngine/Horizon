#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

// 对应参考示例 44-sss 的 fs_sss_deferred_combine.sc：把屏幕空间效果合回场景色。
// 权重已在 ssr_trace_compute.glsl 里算完并存进 ssr.a，这里只做插值 + 调试视图。

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 2, binding = 0, rgba16f) uniform image2D imagesRGBA16F[];

layout(push_constant) uniform PushConsts
{
    uint colorID;   // RGBA16F 直接光结果（a: reflectivity）
    uint ssrID;     // RGBA16F 反射色（a: 权重）
    uint outputID;  // RGBA16F 最终输出
    uint pad0;
    vec4 params0;   // xy: 分辨率, z: intensity, w: debug 模式（0 最终 / 1 仅反射 / 2 权重 / 3 reflectivity）
} pushConsts;

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= int(pushConsts.params0.x) || coord.y >= int(pushConsts.params0.y))
        return;

    vec4 base = imageLoad(imagesRGBA16F[pushConsts.colorID], coord);
    vec4 ssr = imageLoad(imagesRGBA16F[pushConsts.ssrID], coord);

    // SSSR:弹射贡献是加性的一次间接光(吞吐/置信度已折进 rgb)
    vec3 result = base.xyz + ssr.xyz * pushConsts.params0.z;

    int mode = int(pushConsts.params0.w + 0.5);
    if (mode == 1)
        result = ssr.xyz;
    else if (mode == 2)
        result = vec3(ssr.w);
    else if (mode == 3)
        result = base.xyz;

    imageStore(imagesRGBA16F[pushConsts.outputID], coord, vec4(result, 1.0));
}
