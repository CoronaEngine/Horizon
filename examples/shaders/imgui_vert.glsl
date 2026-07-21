#version 450

layout(binding = 0) uniform UiTransform {
    vec2 scale;
    vec2 translate;
} ubo;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    gl_Position = vec4(inPosition * ubo.scale + ubo.translate, 0.0, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
