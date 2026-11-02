// Copyright 2020 Google LLC

#pragma once

#define GLOBAL_SET 0
#define MATERIAL_SET 1
#define PER_OBJECT_SET 2

bool IsNaN(float x)
{
    return (asuint(x) & 0x7FFFFFFF) > 0x7F800000;
}

bool AnyIsNaN(float2 v)
{
    return (IsNaN(v.x) || IsNaN(v.y));
}

bool AnyIsNaN(float3 v)
{
    return (IsNaN(v.x) || IsNaN(v.y) || IsNaN(v.z));
}

bool AnyIsNaN(float4 v)
{
    return (IsNaN(v.x) || IsNaN(v.y) || IsNaN(v.z) || IsNaN(v.w));
}

bool IsInf(float x)
{
    return (asuint(x) & 0x7FFFFFFF) == 0x7F800000;
}

bool AnyIsInf(float2 v)
{
    return (IsInf(v.x) || IsInf(v.y));
}

bool AnyIsInf(float3 v)
{
    return (IsInf(v.x) || IsInf(v.y) || IsInf(v.z));
}

bool AnyIsInf(float4 v)
{
    return (IsInf(v.x) || IsInf(v.y) || IsInf(v.z) || IsInf(v.w));
}

bool IsFinite(float x)
{
    return (asuint(x) & 0x7F800000) != 0x7F800000;
}

[[vk::binding(0, GLOBAL_SET)]] cbuffer global_ubo : register(b0) 
{ 
	float4x4 projectionMatrix;

	float4x4 viewMatrix;
};

[[vk::binding(1, GLOBAL_SET)]]Texture2D shadow_map : register(t1);
