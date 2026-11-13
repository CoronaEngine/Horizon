#version 450
#include "raster_batch.slang"
layout(location = 15) flat in uint drawParameterWord;

// 移植自参考示例 21-deferred 的 fs_deferred_combine.sc：
// albedo × light（线性空间相乘再回 gamma）。

#extension GL_EXT_nonuniform_qualifier : enable

// 材质纹理位于 set 0，索引来自片元阶段的 BDA 绘制参数。
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

struct DeferredCombinePC {
    uint gAlbedoIndex;
    uint gLightIndex;
};
#define fpc (*(DeferredCombinePC*)(drawRoot.parameters + drawParameterWord))

#define gAlbedo combinedTextureSamplerHandles[fpc.gAlbedoIndex]
#define gLight  combinedTextureSamplerHandles[fpc.gLightIndex]

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 albedo = pow(texture(gAlbedo, v_texcoord).xyz, vec3(2.2)); // toLinear
    vec3 light = pow(texture(gLight, v_texcoord).xyz, vec3(2.2));
    outColor = vec4(pow(albedo * light, vec3(1.0 / 2.2)), 1.0); // toGamma
}
