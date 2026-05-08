#version 450
#extension GL_EXT_nonuniform_qualifier : enable

//layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec4 outColor;

layout(binding = 2) uniform sampler2D texSampler;

void main()
{
    //outColor = vec4(inColor, 1.0);
    outColor = texture(texSampler, inUV);
}