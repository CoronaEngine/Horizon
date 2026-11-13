#version 450
#include "raster_batch.slang"
layout(location = 15) flat in uint drawParameterWord;

// 移植自参考示例 21-deferred 的 fs_deferred_geom.sc：G-buffer 单 pass MRT 输出
// （albedo / 世界法线 encodeNormalUint / 器件深度值）。

#extension GL_EXT_nonuniform_qualifier : enable

// bindless combined-image-sampler 表（set 0）+ 与 deferred_geom_vert.glsl 共享的 BDA 绘制参数。
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

// 与 deferred_geom_vert.glsl 共享 BDA 绘制参数布局。
struct DeferredGeomPC {
    mat4 model;
    uint texColorIndex;
    uint texNormalIndex;
};
#define model_pc_fs (*(DeferredGeomPC*)(drawRoot.parameters + drawParameterWord))

#define texColor  combinedTextureSamplerHandles[model_pc_fs.texColorIndex]
#define texNormal combinedTextureSamplerHandles[model_pc_fs.texNormalIndex]

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_tangent;
layout(location = 2) in vec3 v_bitangent;
layout(location = 3) in vec2 v_texcoord;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outDepthVal;

void main()
{
    outAlbedo = texture(texColor, v_texcoord);

    vec3 normal;
    normal.xy = texture(texNormal, v_texcoord).xy * 2.0 - 1.0;
    normal.z = sqrt(max(0.0, 1.0 - dot(normal.xy, normal.xy)));

    mat3 tbn = mat3(normalize(v_tangent), normalize(v_bitangent), normalize(v_normal));
    vec3 wnormal = normalize(tbn * normal);
    outNormal = vec4(wnormal * 0.5 + 0.5, 1.0); // encodeNormalUint

    outDepthVal = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
