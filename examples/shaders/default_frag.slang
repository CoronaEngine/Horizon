[shader("fragment")]
float4 main(float3 fragColor : LOCATION0) : SV_TARGET0
{
    return float4(fragColor, 1.0);
}