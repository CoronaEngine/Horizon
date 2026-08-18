#version 450
#extension GL_ARB_shader_draw_parameters : enable

// P2: per-draw data moved to SSBO, indexed by gl_DrawID.
// Push constant now only holds the SSBO descriptor index.

layout(push_constant) uniform DrawStressPC {
    uint per_draw_ssbo_index;
} model_pc;

struct PerDrawData {
    mat4 mvp;
};

layout(set = 1, binding = 0) readonly buffer PerDrawSSBO {
    PerDrawData draws[];
} per_draw_data[];

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 v_color;

void main()
{
    // DEBUG: clamp drawIdx to ensure we don't go out of bounds
    uint drawIdx = gl_DrawID;

    // Fallback: if descriptor index is 0xFFFFFFFF, use identity matrix (will show cubes at origin)
    if (model_pc.per_draw_ssbo_index == 0xFFFFFFFF)
    {
        gl_Position = vec4(inPosition * 0.25, 1.0);
        v_color = vec3(1.0, 0.0, 1.0); // magenta = error indicator
        return;
    }

    PerDrawData draw = per_draw_data[model_pc.per_draw_ssbo_index].draws[drawIdx];
    gl_Position = draw.mvp * vec4(inPosition, 1.0);
    v_color = inColor;
}
