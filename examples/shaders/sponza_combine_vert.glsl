#version 450

// Sponza deferred combine, vertex stage: full-screen quad from (0,0)..(1,1).

layout(location = 0) in vec3 inCorner;

layout(location = 0) out vec2 v_texcoord;

void main()
{
    gl_Position = vec4(inCorner.xy * 2.0 - 1.0, 0.0, 1.0);
    v_texcoord = inCorner.xy;
}
