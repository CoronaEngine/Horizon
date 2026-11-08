#version 450
#extension GL_EXT_nonuniform_qualifier : enable
layout(push_constant) uniform OverlayPC { uint sceneIndex; uint overlayIndex; } pc;
layout(set = 2, binding = 0, rgba16f) uniform readonly image2D storageImages[];
layout(location = 0) out vec4 outColor;
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 scene = imageLoad(storageImages[pc.sceneIndex], pixel);
    vec4 overlay = imageLoad(storageImages[pc.overlayIndex], pixel);
    // UI rendered over transparent black is already premultiplied.
    outColor = vec4(overlay.rgb + scene.rgb * (1.0 - overlay.a), 1.0);
}
