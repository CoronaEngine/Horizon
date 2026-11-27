struct ViewProj {
    column_major float4x4 view;
    column_major float4x4 proj;
    uint texIndex;
};
[[vk::binding(0, 3)]] ConstantBuffer<ViewProj> vp;
[[vk::binding(0, 0)]] Sampler2D<float4> combinedTextureSamplerHandles[];

[shader("fragment")]
void main(float2 texCoord : TEXCOORD0, out float4 outColor : SV_Target0) {
    outColor = combinedTextureSamplerHandles[vp.texIndex].Sample(texCoord);
}
