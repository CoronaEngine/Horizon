// The first two device addresses precede the parameter buffer in the raster push constants.
struct HorizonRasterRoot {
    uint* packets;
    uint* geometries;
    uint* parameters;
    uint groupBase;
};
[[vk::push_constant]] ConstantBuffer<HorizonRasterRoot> drawRoot;

struct DrawParameters {
    column_major float4x4 model;
    uint texIndex;
};

[[vk::binding(0, 0)]] Sampler2D<float4> combinedTextureSamplerHandles[];

[shader("fragment")]
void main(float2 texCoord : TEXCOORD0,
          nointerpolation uint parameterWord : TEXCOORD1, out float4 outColor : SV_Target0) {
    DrawParameters* draw = (DrawParameters*)(drawRoot.parameters + parameterWord);
    outColor = combinedTextureSamplerHandles[draw->texIndex].Sample(texCoord);
}
