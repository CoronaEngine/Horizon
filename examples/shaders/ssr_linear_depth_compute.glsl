#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

// 移植自参考示例 44-sss 的 fs_sss_linear_depth.sc：器件深度 → view 空间线性深度。
//
// 原版用 near/far 手算 depthLinearizeMul/Add：
//     viewZ = mul / (add - device_z),  mul = far*near/(far-near), add = far/(far-near)
// 这里改成直接由投影矩阵取系数（同 example_assao 的做法），
//     device_z = p22 + p32/viewZ  ⇒  viewZ = p32 / (device_z - p22)
// 与深度范围约定（0..1 或 -1..1）无关，比手算 near/far 稳。

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 2, binding = 0, r32f) uniform image2D imagesR32F[];

layout(push_constant) uniform PushConsts
{
    uint depthID;        // R32F 器件深度（G-buffer 输出）
    uint linearDepthID;  // R32F view 空间线性深度（本站输出）
    uint pad0;
    uint pad1;
    vec4 depth_unpack;   // xy: viewZ = x / (device_z + y)，即 x=p32, y=-p22
    vec4 params0;        // xy: 分辨率, z: far（无几何处的填充值）, w: 未用
} pushConsts;

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= int(pushConsts.params0.x) || coord.y >= int(pushConsts.params0.y))
        return;

    float device_z = imageLoad(imagesR32F[pushConsts.depthID], coord).x;
    float denom = device_z + pushConsts.depth_unpack.y;

    // 远平面（清屏值）处 denom → 0，钳到 far 避免 inf 污染后续步进比较
    float view_z = (abs(denom) < 1e-6) ? pushConsts.params0.z
                                       : pushConsts.depth_unpack.x / denom;
    view_z = clamp(view_z, 0.0, pushConsts.params0.z);

    imageStore(imagesR32F[pushConsts.linearDepthID], coord, vec4(view_z, 0.0, 0.0, 0.0));
}
