#version 450

// Same scene layout as ibl_vert.glsl: mesh + skybox in one pass (pc.mat3.y = isSkybox).

layout(set = 3, binding = 0) uniform DisneyPbrShared {
    mat4 proj_view;
    mat4 skyEnvMtx;
    mat4 envMtx;
    vec4 camPos;
    vec4 flags;     // x: doDiffuse, y: doSpecular, z: doDiffuseIbl, w: doSpecularIbl
    vec4 lightDir;
    vec4 lightCol;
    vec4 exposurePad; // x: exposure
} vsp;

layout(push_constant) uniform DisneyPbrPC {
    mat4 model;
    vec4 mat0; // rgb: baseColor, a: metallic
    vec4 mat1; // x: roughness, y: specular, z: specularTint, w: subsurface
    vec4 mat2; // x: anisotropic, y: sheen, z: sheenTint, w: clearcoat
    vec4 mat3; // x: clearcoatGloss, y: isSkybox, z: aspect, w: bgType
    uint texCubeIndex;
    uint texCubeIrrIndex;
} vpc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 v_view;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec3 v_dir;
layout(location = 3) out vec3 v_worldPos;

void main()
{
    if (vpc.mat3.y > 0.5)
    {
        gl_Position = vec4(inPosition.xy, 1.0, 1.0);

        float fovHeight = tan(radians(45.0) * 0.5);
        float aspect    = vpc.mat3.z;
        vec2 tex = inPosition.xy * vec2(fovHeight * aspect, -fovHeight);
        v_dir = (vsp.skyEnvMtx * vec4(tex, 1.0, 0.0)).xyz;

        v_view     = vec3(0.0);
        v_normal   = vec3(0.0, 0.0, 1.0);
        v_worldPos = vec3(0.0);
    }
    else
    {
        mat4 mvp = vsp.proj_view * vpc.model;
        gl_Position = mvp * vec4(inPosition, 1.0);
        v_worldPos = (vpc.model * vec4(inPosition, 1.0)).xyz;
        v_view     = vsp.camPos.xyz - v_worldPos;
        v_normal   = (vpc.model * vec4(inNormal, 0.0)).xyz;
        v_dir      = vec3(0.0);
    }
}
