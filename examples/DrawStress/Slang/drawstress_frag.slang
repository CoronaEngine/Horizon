[shader("fragment")]
void main(float3 color : COLOR0, out float4 outColor : SV_Target0) {
    outColor = float4(color, 1.0);
}
