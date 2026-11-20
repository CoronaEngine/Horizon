#include "../raster_batch.slang"

struct DisneyVaryings {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 view : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(15)]] nointerpolation uint parameterWord : TEXCOORD15;
};

struct DisneyPbrShared {
    column_major float4x4 proj_view;
    column_major float4x4 envMtx;
    float4 camPos;
    float4 flags;       // Diffuse, specular, diffuse IBL, specular IBL
    float4 lightDir;
    float4 lightCol;
    float4 exposurePad; // x: exposure
};

struct DisneyDraw {
    column_major float4x4 model;
    float4 mat0; // baseColor.rgb, metallic
    float4 mat1; // roughness, specular, specularTint, subsurface
    float4 mat2; // anisotropic, sheen, sheenTint, clearcoat
    float clearcoatGloss;
    uint texCubeIndex;
    uint texCubeIrrIndex;
};

DisneyDraw* getDisneyDraw(uint parameterWord) {
    return (DisneyDraw*)(drawRoot.parameters + parameterWord);
}
