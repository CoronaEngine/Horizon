#version 450

// RSM 场景 pass FS：
//   直接光：世界空间 Blinn-Phong 聚光 + 距离衰减 + 硬阴影
//           （阴影深度存 RSM position 图的 w 通道，省一张打包深度图）
//   间接光：RSM gather —— 把着色点投到光空间 uv，在周围按论文的
//           重要性分布采 64 个像素光，按式(1) 累加单次弹射：
//             E_p = Φ_p · max(0,⟨n_p|x-x_p⟩)·max(0,⟨n|x_p-x⟩) / ‖x-x_p‖⁴
//           采样密度 ∝ 1/r，用 ξ1² 权重补偿（偏差换少样本下的低噪声）。
// 采样序列取黄金比例螺旋（确定性，无需随机纹理，EDSL 版展开成同一组常量）。

#extension GL_EXT_nonuniform_qualifier : enable

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform RsmSceneShared {
    mat4 proj_view;
    mat4 light_proj_view;
    vec4 light_pos_ws;
    vec4 light_dir_ws;
    vec4 light_color;
    vec4 spot_params;
    vec4 rsm_params;
    vec4 camera_pos_ws;
} fsp;

// bindless combined-image-sampler 表（set 0）
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

// 与 rsm_scene_vert.glsl 共享同一 push constant 块（布局一致）。EDSL 同时继承
// vert/frag 的 ResourceBindings，两 stage 实例名必须不同以避免 C++ 端二义（C2385）。
layout(push_constant) uniform RsmScenePC {
    mat4 model;
    vec4 albedo;
    uint rsmPositionIndex;
    uint rsmNormalIndex;
    uint rsmFluxIndex;
} model_pc_fs;

#define rsmPositionMap combinedTextureSamplerHandles[model_pc_fs.rsmPositionIndex]
#define rsmNormalMap   combinedTextureSamplerHandles[model_pc_fs.rsmNormalIndex]
#define rsmFluxMap     combinedTextureSamplerHandles[model_pc_fs.rsmFluxIndex]

layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec4 v_shadowcoord;

layout(location = 0) out vec4 outColor;

const int RSM_SAMPLE_COUNT = 64;
const float TWO_PI = 6.28318530718;
const float GOLDEN = 0.61803398875;

float hardShadow(vec4 shadowCoord, float bias)
{
    vec2 uv = shadowCoord.xy / shadowCoord.w;
    if (any(greaterThan(uv, vec2(1.0))) || any(lessThan(uv, vec2(0.0))))
        return 1.0;
    float receiver = shadowCoord.z / shadowCoord.w - bias;
    float occluder = texture(rsmPositionMap, uv).w;
    return step(receiver, occluder);
}

vec3 indirectLighting(vec2 rsm_uv, vec3 x, vec3 n)
{
    vec3 sum = vec3(0.0);
    for (int i = 0; i < RSM_SAMPLE_COUNT; ++i)
    {
        float xi1 = (float(i) + 0.5) / float(RSM_SAMPLE_COUNT);
        float xi2 = fract(float(i) * GOLDEN);
        vec2 uv = rsm_uv + fsp.rsm_params.x * xi1 * vec2(cos(TWO_PI * xi2), sin(TWO_PI * xi2));
        uv = clamp(uv, vec2(0.0), vec2(1.0)); // 出界钳到边缘（清屏 texel flux=0，无贡献）

        vec3 xp   = texture(rsmPositionMap, uv).xyz;
        vec3 np   = texture(rsmNormalMap, uv).xyz;
        vec3 flux = texture(rsmFluxMap, uv).rgb;

        vec3 w = x - xp;                // 像素光 → 着色点（未归一化，d² 折进分母）
        float d2 = max(dot(w, w), 1.0); // 近距钳制防 firefly
        float e = max(0.0, dot(np, w)) * max(0.0, dot(n, -w)) / (d2 * d2);
        sum += flux * (e * xi1 * xi1);  // ξ1² 补偿中心密集的采样分布
    }
    return sum;
}

void main()
{
    vec3 n = normalize(v_world_normal);
    vec3 x = v_world_pos;

    // 直接光：聚光 Blinn-Phong + 距离衰减
    vec3 to_light = fsp.light_pos_ws.xyz - x;
    float dist = length(to_light);
    vec3 l = to_light / dist;
    float cosang = dot(-l, fsp.light_dir_ws.xyz);
    float spot = smoothstep(fsp.spot_params.y, fsp.spot_params.x, cosang);
    float attn = fsp.light_pos_ws.w / max(dist * dist, 1.0);

    vec3 v = normalize(fsp.camera_pos_ws.xyz - x);
    vec3 h = normalize(l + v);
    float ndotl = max(0.0, dot(n, l));
    float spec = pow(max(0.0, dot(n, h)), 32.0) * 0.25;

    float visibility = hardShadow(v_shadowcoord, fsp.spot_params.z);

    vec3 albedo = model_pc_fs.albedo.rgb;
    vec3 direct = (albedo * ndotl + vec3(spec)) * fsp.light_color.rgb * (spot * attn * visibility);
    vec3 ambient = albedo * fsp.light_dir_ws.w;

    // 间接光：着色点投到 RSM uv，在周围 gather 像素光
    vec2 rsm_uv = clamp(v_shadowcoord.xy / v_shadowcoord.w, vec2(0.0), vec2(1.0));
    vec3 indirect = albedo * indirectLighting(rsm_uv, x, n) * fsp.rsm_params.y;

    vec3 final = ambient + direct + indirect;
    if (fsp.rsm_params.z > 1.5)      final = indirect;      // debug 2: 只看间接光
    else if (fsp.rsm_params.z > 0.5) final = ambient + direct; // debug 1: 关掉间接光

    outColor = vec4(pow(abs(final), vec3(1.0 / 2.2)), 1.0);
}
