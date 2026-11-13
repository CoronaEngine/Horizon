#version 450
#include "raster_batch.slang"
layout(location = 2) flat in uint drawParameterWord;
#extension GL_EXT_nonuniform_qualifier : enable

// Horizon bindless combined-image-sampler 表（set 0）。纹理经 image.storeDescriptor()
// 存入该表后返回索引，通过 BDA 绘制参数 传入 shader 索引采样。
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

// 与 baseline_vert.glsl 共享 BDA 绘制参数布局。
struct PushConsts {
    mat4 model;
    uint texIndex;
};
#define pc_fs (*(PushConsts*)(drawRoot.parameters + drawParameterWord))

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(combinedTextureSamplerHandles[pc_fs.texIndex], fragTexCoord);
}
