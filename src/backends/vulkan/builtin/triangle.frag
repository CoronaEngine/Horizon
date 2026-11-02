// Copyright 2020 Google LLC

float4 main([[vk::location(0)]] float4 Color : COLOR0) : SV_TARGET
{
  return float4(Color.rgb, 1.0);
}