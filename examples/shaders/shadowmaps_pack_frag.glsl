#version 450

// 移植自参考示例 16-shadowmaps 的 fs_shadowmaps_packdepth.sc（InvZ）：
// z/w 打包进 RGBA8。Vulkan 裁剪 z 已在 [0,1]，不做 原版的 *0.5+0.5 重映射
// （采样侧的 bias 矩阵 z 也相应用恒等映射，两侧一致即可）。

layout(location = 0) in vec4 v_position;

layout(location = 0) out vec4 outColor;

vec4 packFloatToRgba(float value)
{
    const vec4 shift = vec4(256.0 * 256.0 * 256.0, 256.0 * 256.0, 256.0, 1.0);
    const vec4 mask = vec4(0.0, 1.0 / 256.0, 1.0 / 256.0, 1.0 / 256.0);
    vec4 comp = fract(value * shift);
    comp -= comp.xxyz * mask;
    return comp;
}

void main()
{
    float depth = v_position.z / v_position.w;
    outColor = packFloatToRgba(depth);
}
