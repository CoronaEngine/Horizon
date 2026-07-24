#version 450
#extension GL_EXT_nonuniform_qualifier : enable

// Horizon bindless combined-image-sampler 表（set 0）。字体图集经
// image.store_descriptor() 存入该表后返回索引，经 push constant 传入采样。
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

layout(push_constant) uniform PushConsts {
    uint texIndex;
} pc;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor * texture(combinedTextureSamplerHandles[pc.texIndex], fragTexCoord);
}
