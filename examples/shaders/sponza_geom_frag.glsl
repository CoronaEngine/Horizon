#version 450

// Sponza G-buffer geometry pass, fragment stage: single-pass MRT writing
// albedo / world normal / device depth, matching deferred_geom_mrt_frag.glsl.
//
// The two spare alpha channels carry the surface response so no fourth render
// target is needed:
//   albedo.a -> specular intensity (from the material's '*_spec' map)
//   normal.a -> glossiness         (from the material's roughness)

#extension GL_EXT_nonuniform_qualifier : enable

// bindless combined-image-sampler table (set 0).
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

// Shared with sponza_geom_vert.glsl; the layout must stay identical.
layout(set = 3, binding = 0) uniform SponzaGeomShared {
    mat4 view_proj;
    vec4 spec_range;
    vec4 no_map_material;
} fsp;

// Shares the push constant block with sponza_geom_vert.glsl; the layout must
// match exactly. The EDSL inherits ResourceBindings from both stages, so the
// two instances need different names to stay unambiguous on the C++ side.
layout(push_constant) uniform SponzaGeomPC {
    vec4 base_color_factor;
    uint tex_base_color_index;
    uint tex_normal_index;
    uint tex_specular_index;
    uint tex_mask_index;
    uint material_flags;
} model_pc_fs;

const uint MATERIAL_HAS_NORMAL = 1u;
const uint MATERIAL_ALPHA_MASK = 2u;
const uint MATERIAL_HAS_SPECULAR = 4u;

#define texBaseColor combinedTextureSamplerHandles[model_pc_fs.tex_base_color_index]
#define texNormal    combinedTextureSamplerHandles[model_pc_fs.tex_normal_index]
#define texSpecular  combinedTextureSamplerHandles[model_pc_fs.tex_specular_index]
#define texMask      combinedTextureSamplerHandles[model_pc_fs.tex_mask_index]

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_tangent;
layout(location = 2) in vec3 v_bitangent;
layout(location = 3) in vec2 v_texcoord;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outDepthVal;

void main()
{
    if ((model_pc_fs.material_flags & MATERIAL_ALPHA_MASK) != 0u)
    {
        // Foliage and chains are cut out by a separate mask texture; the glTF
        // alphaMode is OPAQUE for every Sponza material, so the mask is the only
        // signal available here.
        if (texture(texMask, v_texcoord).r < 0.5)
            discard;
    }

    vec4 base = texture(texBaseColor, v_texcoord) * model_pc_fs.base_color_factor;

    // The '*_spec' map is the only per-material surface signal this scene has:
    // every glTF material shares one metallicFactor (0.588) and one
    // roughnessFactor (0.9), so material.roughness carries no information at all.
    // The map drives BOTH specular intensity and glossiness, because a constant
    // gloss gives every surface the same lobe width and materials stop reading
    // apart -- polished floor and matte fabric only differed in brightness.
    float specular = fsp.no_map_material.x;
    float gloss = fsp.no_map_material.y;
    if ((model_pc_fs.material_flags & MATERIAL_HAS_SPECULAR) != 0u)
    {
        float specSample = texture(texSpecular, v_texcoord).r;
        specular = mix(fsp.spec_range.x, fsp.spec_range.y, specSample);
        gloss = mix(fsp.spec_range.z, fsp.spec_range.w, specSample);
    }

    vec3 n = normalize(v_normal);
    if ((model_pc_fs.material_flags & MATERIAL_HAS_NORMAL) != 0u)
    {
        // Tangent-space normal map, produced offline by differentiating the
        // upstream grayscale height map (tools/sponza/convert_sponza.py).
        vec3 tangent_normal = texture(texNormal, v_texcoord).xyz * 2.0 - 1.0;
        mat3 tbn = mat3(normalize(v_tangent), normalize(v_bitangent), n);
        n = normalize(tbn * tangent_normal);
    }

    outAlbedo = vec4(base.rgb, specular);
    outNormal = vec4(n * 0.5 + 0.5, gloss); // encodeNormalUint
    outDepthVal = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
