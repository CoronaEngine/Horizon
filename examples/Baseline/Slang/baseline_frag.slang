#include "baseline_draw.slang"

[[vk::binding(0, 0)]] Sampler2D<float4> combinedTextureSamplerHandles[];

[shader("fragment")]
void main(float3 color : COLOR0, float2 texCoord : TEXCOORD0,
          nointerpolation uint parameterWord : TEXCOORD1, out float4 outColor : SV_Target0) {
    uint texIndex = getDrawParameters(parameterWord)->texIndex;
    outColor = combinedTextureSamplerHandles[texIndex].Sample(texCoord);
}
