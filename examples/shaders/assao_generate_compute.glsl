#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

// 移植自参考示例 39-assao 的 cs_assao_generate_q1.sc 思路的简化实现：
// 全分辨率单 pass（不做原版的 4 slice 半分辨率去交错），12-tap 螺旋盘采样，
// 参数取原版默认（radius 1.2 / multiplier 1.0 / power 1.5 /
// horizonAngleThreshold 0.06 / fadeOut 50→200）。

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 2, binding = 0, r32f) uniform image2D imagesR32F[];
layout(set = 2, binding = 0, rgba8) uniform image2D imagesRGBA8[];

layout(push_constant) uniform PushConsts
{
    uint depthID;   // R32F 器件深度
    uint normalID;  // RGBA8 view 空间法线
    uint aoID;      // R32F AO 输出
    uint pad0;
    vec4 depth_unpack;   // xy: viewZ = x / (device_z + y)
    vec4 ndc_to_view;    // xy: mul, zw: add（uv → view.xy / viewZ）
    vec4 params0;        // x: radius, y: shadowMultiplier, z: shadowPower, w: horizonAngleThreshold
    vec4 params1;        // x: fadeOutFrom, y: fadeOutTo, zw: 分辨率
} pushConsts;

const float PI = 3.14159265359;

vec3 load_view_pos(ivec2 coord, vec2 resolution)
{
    float device_z = imageLoad(imagesR32F[pushConsts.depthID], coord).x;
    float view_z = pushConsts.depth_unpack.x / (device_z + pushConsts.depth_unpack.y);
    vec2 uv = (vec2(coord) + 0.5) / resolution;
    vec2 view_xy = (uv * pushConsts.ndc_to_view.xy + pushConsts.ndc_to_view.zw) * view_z;
    return vec3(view_xy, view_z);
}

float interleaved_gradient_noise(vec2 p)
{
    const vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(p, magic.xy)));
}

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    vec2 resolution = pushConsts.params1.zw;
    if (coord.x >= int(resolution.x) || coord.y >= int(resolution.y))
        return;

    vec3 pos = load_view_pos(coord, resolution);
    vec3 normal = imageLoad(imagesRGBA8[pushConsts.normalID], coord).xyz * 2.0 - 1.0;

    float radius = pushConsts.params0.x;
    float horizon_threshold = pushConsts.params0.w;

    // 螺旋盘 12 tap，逐像素随机旋转
    const int num_taps = 12;
    float rot = interleaved_gradient_noise(vec2(coord)) * 2.0 * PI;
    float screen_radius = radius / max(pos.z, 0.1) * resolution.y * 0.5 / abs(pushConsts.ndc_to_view.y);

    float occlusion = 0.0;
    float weight_sum = 0.0001;
    for (int i = 0; i < num_taps; ++i)
    {
        float angle = rot + float(i) * (2.0 * PI * 2.5 / float(num_taps));
        float dist = (float(i) + 0.5) / float(num_taps);
        dist = dist * dist; // 内密外疏
        vec2 offset = vec2(cos(angle), sin(angle)) * dist * screen_radius;

        ivec2 tap = coord + ivec2(offset);
        tap = clamp(tap, ivec2(0), ivec2(resolution) - 1);

        vec3 tap_pos = load_view_pos(tap, resolution);
        vec3 diff = tap_pos - pos;
        float len = length(diff);
        if (len < 0.001)
            continue;

        float ndotd = dot(normal, diff / len) - horizon_threshold;
        float falloff = clamp(1.0 - len / radius, 0.0, 1.0);
        occlusion += clamp(ndotd, 0.0, 1.0) * falloff;
        weight_sum += 1.0;
    }
    occlusion /= weight_sum;

    // 效果强度与距离淡出
    float ao = clamp(1.0 - occlusion * pushConsts.params0.y * 2.0, 0.0, 1.0);
    ao = pow(ao, pushConsts.params0.z);

    float fade = clamp((pushConsts.params1.y - pos.z) / max(pushConsts.params1.y - pushConsts.params1.x, 0.001), 0.0, 1.0);
    ao = mix(1.0, ao, fade);

    imageStore(imagesR32F[pushConsts.aoID], coord, vec4(ao, 0.0, 0.0, 0.0));
}
