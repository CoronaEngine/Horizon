struct VSOutput
{
    float4 pos : SV_POSITION;
    float3 fragColor : LOCATION0;
};

[shader("vertex")]
VSOutput main(float3 inPosition : LOCATION0, float3 inColor : LOCATION1)
{
    VSOutput output;
    output.pos = float4(inPosition, 1.0);
    output.fragColor = inColor;
    return output;
}