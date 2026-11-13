#version 450
layout(set = 3, binding = 0) uniform Scene {
    mat4 view;
    mat4 proj;
    uint instanceBufferID;
} vp;

// Write device depth into R32F for Hi-Z mip0 (compute cannot sample D32).

layout(location = 0) out vec4 outDepth;

void main()
{
    outDepth = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
