#version 450
#include "raster_batch.slang"
layout(location = 15) flat in uint hzParameterWord;
layout(set = 3, binding = 0) uniform UiTransform { vec2 scale; vec2 translate; } ubo;
#extension GL_EXT_nonuniform_qualifier : enable

// Font atlas in the bindless combined-image-sampler table.
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

struct UiDraw {
    uint texIndex;
};
#define pc (*(UiDraw*)(drawRoot.parameters + hzParameterWord))

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor * texture(combinedTextureSamplerHandles[pc.texIndex], fragTexCoord);
}
