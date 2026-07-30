#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

// 移植自参考示例 44-sss 的 fs_screen_space_shadows.sc —— 屏幕空间射线步进内核。
//
// 与原版的对应关系（步进循环结构逐行照搬，只换掉射线方向和命中后的处理）：
//   原版 lightStep = normalize(u_lightPosition - viewSpacePosition)  指向光源
//   这里 ray_dir   = reflect(normalize(view_pos), n)                 镜面反射方向
//
//   原版 命中判据  DEPTH_EPSILON < delta && delta < radius   （radius 兼作厚度）
//   这里 命中判据  DEPTH_EPSILON < delta && delta < thickness（厚度独立成参数）
//
//   原版 命中后累加 occluded / softOccluded，最后输出 shadow 标量
//   这里 命中后 binary search 精修交点 → 采样颜色图 → 算合成权重
//
//   原版 用 texture2DLod 采样深度；这里沿用 example_assao 的 imageLoad 路线。
//
// 输出：RGBA16F  rgb = 反射颜色, a = 合成权重（Fresnel × 边缘 fade × roughness × 命中置信度）

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 2, binding = 0, r32f)    uniform image2D imagesR32F[];
layout(set = 2, binding = 0, rgba8)   uniform image2D imagesRGBA8[];
layout(set = 2, binding = 0, rgba16f) uniform image2D imagesRGBA16F[];

layout(push_constant) uniform PushConsts
{
    uint linearDepthID; // R32F   view 空间线性深度
    uint normalID;      // RGBA8  xyz: view 法线, w: roughness
    uint colorID;       // RGBA16F xyz: 直接光结果, w: reflectivity
    uint ssrID;         // RGBA16F xyz: 反射色, w: 权重（本站输出）
    vec4 ndc_to_view;   // xy: mul, zw: add（uv → view.xy / viewZ）
    vec4 params0;       // x: proj00, y: proj11, z: maxDistance, w: numSteps
    vec4 params1;       // x: thickness, y: frameIdx, zw: 分辨率
    vec4 params2;       // x: fresnelPower, y: fresnelF0, z: refineSteps, w: useJitter
} pushConsts;

#define DEPTH_EPSILON 1e-4

// view 空间点 → uv。投影矩阵无斜切，只需 p00/p11 两个系数即可（省掉整个 mat4，
// 原版 44-sss 是把 viewToProj 拆成 4 个 vec4 塞进 uniform 数组）。
// 注意 p11 为负（Horizon 侧 proj[1][1] *= -1 做 Vulkan Y 翻转），Y 翻转已包含在内。
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

// 同 example_assao：逐像素随机旋转/抖动用的 interleaved gradient noise
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

    // 先写零：后面任何一处提前返回都表示「本像素无反射」
    imageStore(imagesRGBA16F[pushConsts.ssrID], coord, vec4(0.0));

    // reflectivity 存在颜色图 alpha；清屏 alpha=0，所以天空/空白处天然被跳过
    float reflectivity = imageLoad(imagesRGBA16F[pushConsts.colorID], coord).w;
    if (reflectivity <= 0.001)
        return;

    // 重建 view 空间坐标（同 example_assao 的 load_view_pos）
    float view_z = imageLoad(imagesR32F[pushConsts.linearDepthID], coord).x;
    vec2 uv = (vec2(coord) + 0.5) / resolution;
    vec3 view_pos = vec3((uv * pushConsts.ndc_to_view.xy + pushConsts.ndc_to_view.zw) * view_z, view_z);

    vec4 packed_normal = imageLoad(imagesRGBA8[pushConsts.normalID], coord);
    vec3 n = normalize(packed_normal.xyz * 2.0 - 1.0);
    float roughness = packed_normal.w;

    vec3 v = normalize(view_pos); // 相机在原点，view_pos 即视线方向
    vec3 ray_dir = reflect(v, n);

    float num_steps = max(1.0, pushConsts.params0.w);
    float step_len = pushConsts.params0.z / num_steps;
    vec3 ray_step = ray_dir * step_len;

    // 沿法线偏置半步，避免起点自相交（原版靠 DEPTH_EPSILON + 朝光源的方向天然避开）
    vec3 sample_pos = view_pos + n * (step_len * 0.5);

    // 原版：initialOffset = useNoiseOffset ? (0.5+random) : 1.0，逐帧换 seed
    float random = interleaved_gradient_noise(vec2(coord) + vec2(314.0, 159.0) * pushConsts.params1.y);
    float initial_offset = (pushConsts.params2.w > 0.0) ? (0.5 + random) : 1.0;
    sample_pos += initial_offset * ray_step;

    float thickness = pushConsts.params1.x;

    bool hit = false;
    float hit_step = num_steps;
    vec3 prev_pos = sample_pos;

    for (int i = 0; i < int(num_steps); ++i, sample_pos += ray_step)
    {
        if (sample_pos.z <= 0.01)
            break; // 射线越过相机平面，屏幕空间已无信息

        vec2 s_uv = view_to_uv(sample_pos);
        if (any(lessThan(s_uv, vec2(0.0))) || any(greaterThan(s_uv, vec2(1.0))))
            break; // 出屏

        float scene_z = load_linear_depth(s_uv);
        float delta = sample_pos.z - scene_z;

        // 与原版完全同构的命中判据：穿到表面之后、但没穿过太厚（避免远处误命中）
        if (DEPTH_EPSILON < delta && delta < thickness)
        {
            hit = true;
            hit_step = float(i);
            break;
        }

        prev_pos = sample_pos;
    }

    if (!hit)
        return;

    // binary search 精修：在最后一步的 [prev_pos, sample_pos] 区间里收敛交点。
    // 原版没有这一步（阴影只要 0/1，不在意交点精度），SSR 采颜色必须精修，
    // 否则反射会出现和步长同尺度的锯齿条纹。
    vec3 lo = prev_pos;
    vec3 hi = sample_pos;
    for (int k = 0; k < int(pushConsts.params2.z); ++k)
    {
        vec3 mid = (lo + hi) * 0.5;
        float scene_z = load_linear_depth(view_to_uv(mid));
        if (mid.z > scene_z)
            hi = mid; // 已在表面之后 → 交点更近
        else
            lo = mid;
    }
    vec2 hit_uv = view_to_uv(hi);

    ivec2 hit_coord = clamp(ivec2(hit_uv * resolution), ivec2(0), ivec2(resolution) - 1);
    vec3 refl_color = imageLoad(imagesRGBA16F[pushConsts.colorID], hit_coord).xyz;

    // ---- 合成权重 ----

    // Fresnel（Schlick）：擦过角反射强，正视反射弱
    float ndotv = clamp(dot(n, -v), 0.0, 1.0);
    float f0 = pushConsts.params2.y;
    float fresnel = f0 + (1.0 - f0) * pow(1.0 - ndotv, pushConsts.params2.x);

    // 屏幕边缘淡出：命中点接近边界时信息不可靠
    vec2 fade_lo = smoothstep(vec2(0.0), vec2(0.12), hit_uv);
    vec2 fade_hi = vec2(1.0) - smoothstep(vec2(0.88), vec2(1.0), hit_uv);
    float edge_fade = fade_lo.x * fade_lo.y * fade_hi.x * fade_hi.y;

    // 步进越远置信度越低
    float dist_fade = mix(0.45, 1.0, 1.0 - clamp(hit_step / num_steps, 0.0, 1.0));

    float weight = reflectivity * fresnel * edge_fade * dist_fade * (1.0 - roughness);

    imageStore(imagesRGBA16F[pushConsts.ssrID], coord, vec4(refl_color, clamp(weight, 0.0, 1.0)));
}
