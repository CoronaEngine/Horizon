#version 450

// Sponza sun shadow map, fragment stage.
//
// Depth is written to an R32F colour target rather than packed into RGBA8 the
// way example_shadowmaps does: full float precision costs nothing here and
// removes the pack/unpack step on both sides.

#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

layout(push_constant) uniform SponzaShadowPC {
    uint tex_mask_index;
    uint material_flags;
} model_pc_fs;

const uint MATERIAL_ALPHA_MASK = 2u;

#define texMask combinedTextureSamplerHandles[model_pc_fs.tex_mask_index]

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 outDepth;

void main()
{
    // Foliage and chains must cut out here too, otherwise they cast the shadow
    // of their bounding quads instead of their silhouette.
    if ((model_pc_fs.material_flags & MATERIAL_ALPHA_MASK) != 0u)
    {
        if (texture(texMask, v_texcoord).r < 0.5)
            discard;
    }

    outDepth = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
