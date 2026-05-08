// Copyright 2020 Google LLC
#include "common.hlsl"

struct VSInput
{
[[vk::location(0)]] float3 Pos : POSITION0;
[[vk::location(1)]] float4 Color : COLOR0;
};

struct PushConstants
{
    float4x4 modelMatrix;
};

[[vk::push_constant]]
PushConstants pushConstants;

struct VSOutput
{
	float4 Pos : SV_POSITION;
[[vk::location(0)]] float4 Color : COLOR0;
};

VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;
	output.Color = input.Color;
	output.Pos = mul(projectionMatrix, mul(viewMatrix, mul(pushConstants.modelMatrix, float4(input.Pos.xyz, 1.0))));
	if (AnyIsNaN(output.Pos))
	{
		output.Color = float4(1, 0, 0, 1);
	}
	#ifdef _ALPHA_DISABLE
	output.Color.a = 1;
	#endif
	return output;
}