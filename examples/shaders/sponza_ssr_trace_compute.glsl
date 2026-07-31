#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

// Sponza 版 SSR 射线步进,改自 ssr_trace_compute.glsl。与原版的差异:
//   - G-buffer 法线是世界空间(sponza_geom_frag 写入),这里用 push constant
//     里的 view 旋转(3 行 vec4)转到 view 空间再反射;
//   - roughness 在 normal.a(原版即如此,沿用);
//   - 颜色源是 combine 输出的线性 HDR(a = reflectivity,天空为 0 天然跳过)。
//
// 输出:RGBA16F rgb = 反射颜色(线性), a = 合成权重。

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 2, binding = 0, r32f)    uniform image2D imagesR32F[];
layout(set = 2, binding = 0, rgba8)   uniform image2D imagesRGBA8[];
layout(set = 2, binding = 0, rgba16f) uniform image2D imagesRGBA16F[];

layout(push_constant) uniform PushConsts
{
    uint linearDepthID; // R32F    view 空间线性深度
    uint normalID;      // RGBA8   xyz: 世界法线, w: roughness
    uint colorID;       // RGBA16F 线性 HDR(a: SSR F0/mask)
    uint ssrID;         // RGBA16F 反射色 + 权重(本站输出)
    vec4 ndc_to_view;   // xy: mul, zw: add(uv → view.xy / viewZ)
    vec4 params0;       // x: proj00, y: proj11, z: maxDistance, w: numSteps
    vec4 params1;       // x: thickness, y: frameIdx, zw: 分辨率
    vec4 params2;       // x: fresnelPower, y: unused, z: refineSteps, w: useJitter
    vec4 view_row0;     // view 矩阵旋转部分的三行(法线世界→view 用)
    vec4 view_row1;
    vec4 view_row2;
} pushConsts;

#define DEPTH_EPSILON 1e-4

vec2 view_to_uv(vec3 p)
{
    vec2 ndc = vec2(pushConsts.params0.x * p.x, pushConsts.params0.y * p.y) / p.z;
    return ndc * 0.5 + 0.5;
}

float load_linear_depth(vec2 uv)
{
    ivec2 c = ivec2(uv * pushConsts.params1.zw);
    c = clamp(c, ivec2(0), ivec2(pushConsts.params1.zw) - 1);
    return imageLoad(imagesR32F[pushConsts.linearDepthID], c).x;
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

    imageStore(imagesRGBA16F[pushConsts.ssrID], coord, vec4(0.0));

    float surface_f0 = imageLoad(imagesRGBA16F[pushConsts.colorID], coord).w;
    if (surface_f0 <= 0.001)
        return;

    float view_z = imageLoad(imagesR32F[pushConsts.linearDepthID], coord).x;
    vec2 uv = (vec2(coord) + 0.5) / resolution;
    vec3 view_pos = vec3((uv * pushConsts.ndc_to_view.xy + pushConsts.ndc_to_view.zw) * view_z, view_z);

    vec4 packed_normal = imageLoad(imagesRGBA8[pushConsts.normalID], coord);
    vec3 n_world = normalize(packed_normal.xyz * 2.0 - 1.0);
    // 世界 → view(旋转部分;平移对方向无效)
    vec3 n = normalize(vec3(dot(pushConsts.view_row0.xyz, n_world),
                            dot(pushConsts.view_row1.xyz, n_world),
                            dot(pushConsts.view_row2.xyz, n_world)));
    float roughness = packed_normal.w;

    vec3 v = normalize(view_pos);
    vec3 ray_dir = reflect(v, n);

    float num_steps = max(1.0, pushConsts.params0.w);
    float step_len = pushConsts.params0.z / num_steps;
    vec3 ray_step = ray_dir * step_len;

    float random = interleaved_gradient_noise(vec2(coord) + vec2(314.0, 159.0) * pushConsts.params1.y);
    float initial_offset = (pushConsts.params2.w > 0.0) ? (0.5 + random) : 1.0;
    float thickness = pushConsts.params1.x;
    // Move off the source surface before tracing. Tie the offset to thickness
    // as well as step length so a grazing ray does not immediately hit itself.
    vec3 ray_origin = view_pos + n * max(thickness * 0.35, step_len * 0.12);
    vec3 sample_pos = ray_origin + initial_offset * ray_step;

    bool hit = false;
    float hit_step = num_steps;
    vec3 prev_pos = ray_origin;
    vec2 prev_uv = view_to_uv(prev_pos);
    float prev_scene_z = load_linear_depth(prev_uv);
    float prev_delta = prev_pos.z - prev_scene_z;
    vec3 hit_lo = prev_pos;
    vec3 hit_hi = sample_pos;

    for (int i = 0; i < int(num_steps); ++i)
    {
        if (sample_pos.z <= 0.01)
            break;

        vec2 s_uv = view_to_uv(sample_pos);
        if (any(lessThan(s_uv, vec2(0.0))) || any(greaterThan(s_uv, vec2(1.0))))
            break;

        float scene_z = load_linear_depth(s_uv);
        float delta = sample_pos.z - scene_z;

        // Detect a depth crossing, not merely a sample that happens to land
        // inside the thickness band. The old band-only test missed surfaces
        // whenever one coarse step jumped from in front to behind by >thickness.
        bool crossed = prev_delta <= DEPTH_EPSILON && delta > DEPTH_EPSILON;
        bool close_hit = i > 0 && delta > DEPTH_EPSILON && delta < thickness;
        if (crossed || close_hit)
        {
            hit = true;
            hit_step = float(i);
            hit_lo = prev_pos;
            hit_hi = sample_pos;
            break;
        }

        prev_pos = sample_pos;
        prev_delta = delta;
        sample_pos += ray_step;
    }

    if (!hit)
        return;

    vec3 lo = hit_lo;
    vec3 hi = hit_hi;
    for (int k = 0; k < int(pushConsts.params2.z); ++k)
    {
        vec3 mid = (lo + hi) * 0.5;
        float scene_z = load_linear_depth(view_to_uv(mid));
        if (mid.z > scene_z)
            hi = mid;
        else
            lo = mid;
    }
    vec2 hit_uv = view_to_uv(hi);
    float refined_scene_z = load_linear_depth(hit_uv);
    if (hi.z - refined_scene_z > thickness)
        return;

    ivec2 hit_coord = clamp(ivec2(hit_uv * resolution), ivec2(0), ivec2(resolution) - 1);
    vec3 refl_color = imageLoad(imagesRGBA16F[pushConsts.colorID], hit_coord).xyz;

    float ndotv = clamp(dot(n, -v), 0.0, 1.0);
    // surface_f0 already contains the mirror's material F0. Multiplying it by
    // another Fresnel term (the old path) squared F0 and left a silver mirror
    // with only a few percent reflection at normal incidence.
    float fresnel = surface_f0 +
                    (1.0 - surface_f0) * pow(1.0 - ndotv, pushConsts.params2.x);

    vec2 fade_lo = smoothstep(vec2(0.0), vec2(0.12), hit_uv);
    vec2 fade_hi = vec2(1.0) - smoothstep(vec2(0.88), vec2(1.0), hit_uv);
    float edge_fade = fade_lo.x * fade_lo.y * fade_hi.x * fade_hi.y;

    float dist_fade = 1.0 - 0.65 * smoothstep(0.35, 1.0, hit_step / num_steps);
    float roughness_fade = 1.0 - smoothstep(0.08, 0.50, roughness);

    float weight = fresnel * edge_fade * dist_fade * roughness_fade;

    imageStore(imagesRGBA16F[pushConsts.ssrID], coord, vec4(refl_color, clamp(weight, 0.0, 1.0)));
}
